//
//  MCHTMLRenderer.cpp
//  testUI
//
//  Created by DINH Viêt Hoà on 1/23/13.
//  Copyright (c) 2013 MailCore. All rights reserved.
//

#include "MCPlainTextRenderer.h"

#include <ctemplate/template.h>
#include "MCAddressDisplay.h"
#include "MCDateFormatter.h"
#include "MCSizeFormatter.h"
#include "MCHTMLRendererCallback.h"

using namespace mailcore;

class HTMLRendererIMAPDummyCallback : public HTMLRendererIMAPCallback {
private:
    Array *mRequiredParts;
    
public:
    HTMLRendererIMAPDummyCallback()
    {
        mRequiredParts = Array::array();
        mRequiredParts->retain();
    }
    
    virtual ~HTMLRendererIMAPDummyCallback()
    {
        MC_SAFE_RELEASE(mRequiredParts);
    }
    
    
    virtual Data * dataForIMAPPart(String * folder, IMAPPart * part)
    {
        mRequiredParts->addObject(part);
        return Data::data();
    }
    
    Array * requiredParts()
    {
        return mRequiredParts;
    }

};

enum {
    RENDER_STATE_NONE,
    RENDER_STATE_HAD_ATTACHMENT,
    RENDER_STATE_HAD_ATTACHMENT_THEN_TEXT,
};

struct htmlRendererContext {
    HTMLRendererIMAPCallback * dataCallback;
    HTMLRendererTemplateCallback * htmlCallback;
    int firstRendered;
    String * folder;
    int state;
    // pass == 0 -> render only text parts,
    // pass == 1 -> render only attachments.
    int pass;
    bool hasMixedTextAndAttachments;
    bool firstAttachment;
    bool hasTextPart;
    Array * relatedAttachments;
    Array * attachments;
};

class DefaultTemplateCallback : public Object, public HTMLRendererTemplateCallback {
};

static bool partContainsMimeType(AbstractPart * part, String * mimeType);
static bool singlePartContainsMimeType(AbstractPart * part, String * mimeType);
static bool multipartContainsMimeType(AbstractMultipart * part, String * mimeType);
static bool messagePartContainsMimeType(AbstractMessagePart * part, String * mimeType);

static String * htmlForAbstractPart(AbstractPart * part, htmlRendererContext * context);

static String * renderTemplate(String * templateContent, HashMap * values);

static String * htmlForAbstractMessage(String * folder, AbstractMessage * message,
                                       HTMLRendererIMAPCallback * dataCallback,
                                       HTMLRendererTemplateCallback * htmlCallback,
                                       Array * attachments,
                                       Array * relatedAttachments);

static bool isTextPart(AbstractPart * part, htmlRendererContext * context)
{
    String * mimeType = part->mimeType()->lowercaseString();
    MCAssert(mimeType != NULL);
    
    if (!part->isInlineAttachment()) {
        if (part->isAttachment() || ((part->filename() != NULL) && context->firstRendered)) {
            return false;
        }
    }
    
    if (mimeType->isEqual(MCSTR("text/plain"))) {
        return true;
    }
    else if (mimeType->isEqual(MCSTR("text/html"))) {
        return true;
    }
    else {
        return false;
    }
}


static AbstractPart * preferredPartInMultipartAlternative(AbstractMultipart * part)
{
    int textPart = -1;
    
    for(unsigned int i = 0 ; i < part->parts()->count() ; i ++) {
        AbstractPart * subpart = (AbstractPart *) part->parts()->objectAtIndex(i);
        if (partContainsMimeType(subpart, MCSTR("text/plain"))) {
            textPart = i;
        }
    }
    if (textPart != -1) {
        return (AbstractPart *) part->parts()->objectAtIndex(textPart);
    }
    else {
        return NULL;
    }
}

static bool partContainsMimeType(AbstractPart * part, String * mimeType)
{
    switch (part->partType()) {
        case PartTypeSingle:
            return singlePartContainsMimeType(part, mimeType);
        case PartTypeMessage:
            return messagePartContainsMimeType((AbstractMessagePart *) part, mimeType);
        case PartTypeMultipartMixed:
        case PartTypeMultipartRelated:
        case PartTypeMultipartAlternative:
        case PartTypeMultipartSigned:
            return multipartContainsMimeType((AbstractMultipart *) part, mimeType);
        default:
            return false;
    }
}

static bool singlePartContainsMimeType(AbstractPart * part, String * mimeType)
{
    return part->mimeType()->lowercaseString()->isEqual(mimeType);
}

static bool multipartContainsMimeType(AbstractMultipart * part, String * mimeType)
{
    for(unsigned int i = 0 ; i < part->parts()->count() ; i ++) {
        AbstractPart * subpart = (AbstractPart *) part->parts()->objectAtIndex(i);
        if (partContainsMimeType(subpart, mimeType)) {
            return true;
        }
    }
    return false;
}

static bool messagePartContainsMimeType(AbstractMessagePart * part, String * mimeType)
{
    return partContainsMimeType(part->mainPart(), mimeType);
}

static String * htmlForAbstractMessage(String * folder, AbstractMessage * message,
                                       HTMLRendererIMAPCallback * dataCallback,
                                       HTMLRendererTemplateCallback * htmlCallback,
                                       Array * attachments,
                                       Array * relatedAttachments)
{
    if (htmlCallback == NULL) {
        htmlCallback = new DefaultTemplateCallback();
        ((DefaultTemplateCallback *) htmlCallback)->autorelease();
    }

    String * content;

    if (message->className()->isEqual(MCSTR("mailcore::MessageBuilder"))) {
        content = ((MessageBuilder *) message)->htmlRendering(htmlCallback);
    }else{
        
        AbstractPart * mainPart = NULL;
        
        if (message->className()->isEqual(MCSTR("mailcore::IMAPMessage"))) {
            mainPart = ((IMAPMessage *) message)->mainPart();
        }
        else if (message->className()->isEqual(MCSTR("mailcore::MessageParser"))) {
            mainPart = ((MessageParser *) message)->mainPart();
        }
        MCAssert(mainPart != NULL);
        
        htmlRendererContext context;
        context.dataCallback = dataCallback;
        context.htmlCallback = htmlCallback;
        context.relatedAttachments = NULL;
        context.attachments = NULL;
        context.firstRendered = 0;
        context.folder = folder;
        context.state = RENDER_STATE_NONE;
        
        context.hasMixedTextAndAttachments = false;
        context.pass = 0;
        context.firstAttachment = false;
        context.hasTextPart = false;
        
        htmlForAbstractPart(mainPart, &context);
        
        context.relatedAttachments = relatedAttachments;
        context.attachments = attachments;
        context.hasMixedTextAndAttachments = (context.state == RENDER_STATE_HAD_ATTACHMENT_THEN_TEXT);
        context.pass = 1;
        context.firstAttachment = false;
        context.hasTextPart = false;
        
        htmlCallback->setMixedTextAndAttachmentsModeEnabled(context.hasMixedTextAndAttachments);
        
        content = htmlForAbstractPart(mainPart, &context);
    }
    
    return content;
}

static String * htmlForAbstractSinglePart(AbstractPart * part, htmlRendererContext * context);
static String * htmlForAbstractMessagePart(AbstractMessagePart * part, htmlRendererContext * context);
static String * htmlForAbstractMultipartRelated(AbstractMultipart * part, htmlRendererContext * context);
static String * htmlForAbstractMultipartMixed(AbstractMultipart * part, htmlRendererContext * context);
static String * htmlForAbstractMultipartAlternative(AbstractMultipart * part, htmlRendererContext * context);

static String * htmlForAbstractPart(AbstractPart * part, htmlRendererContext * context)
{
    String * result = NULL;;
    switch (part->partType()) {
        case PartTypeSingle:
            result = htmlForAbstractSinglePart((AbstractPart *) part, context);
            break;
        case PartTypeMessage:
            result = htmlForAbstractMessagePart((AbstractMessagePart *) part, context);
            break;
        case PartTypeMultipartMixed:
            result = htmlForAbstractMultipartMixed((AbstractMultipart *) part, context);
            break;
        case PartTypeMultipartRelated:
            result = htmlForAbstractMultipartRelated((AbstractMultipart *) part, context);
            break;
        case PartTypeMultipartAlternative:
            result = htmlForAbstractMultipartAlternative((AbstractMultipart *) part, context);
            break;
        case PartTypeMultipartSigned:
            result = htmlForAbstractMultipartMixed((AbstractMultipart *) part, context);
            break;
        default:
            MCAssert(0);
    }
    return result;
}

static String * htmlForAbstractSinglePart(AbstractPart * part, htmlRendererContext * context)
{
    String * mimeType = NULL;
    if (part->mimeType() != NULL) {
        mimeType = part->mimeType()->lowercaseString();
    }
    MCAssert(mimeType != NULL);
    
    if (isTextPart(part, context)) {
        if (context->pass == 0) {
            if (context->state == RENDER_STATE_HAD_ATTACHMENT) {
                context->state = RENDER_STATE_HAD_ATTACHMENT_THEN_TEXT;
            }
            return NULL;
        }
        
        context->hasTextPart = true;
        
        if (mimeType->isEqual(MCSTR("text/plain"))) {
            String * charset = part->charset();
            Data * data = NULL;
            if (part->className()->isEqual(MCSTR("mailcore::IMAPPart"))) {
                data = context->dataCallback->dataForIMAPPart(context->folder, (IMAPPart *) part);
            }
            else if (part->className()->isEqual(MCSTR("mailcore::Attachment"))) {
                data = ((Attachment *) part)->data();
                MCAssert(data != NULL);
            }
            if (data == NULL)
                return NULL;
            
            String * str = data->stringWithDetectedCharset(charset, false);
            //str = str->htmlMessageContent();
            //str = context->htmlCallback->filterHTMLForPart(str);
            context->firstRendered = true;
            return str;
        }
    }
    return NULL;
}

static String * htmlForAbstractMessagePart(AbstractMessagePart * part, htmlRendererContext * context)
{
    if (context->pass == 0) {
        return NULL;
    }
    String * substring = htmlForAbstractPart(part->mainPart(), context);
    return substring;
}

String * htmlForAbstractMultipartAlternative(AbstractMultipart * part, htmlRendererContext * context)
{
    AbstractPart * preferredAlternative = preferredPartInMultipartAlternative(part);
    if (preferredAlternative == NULL)
        return NULL;

    String * html = htmlForAbstractPart(preferredAlternative, context);
    return html;
}

static String * htmlForAbstractMultipartMixed(AbstractMultipart * part, htmlRendererContext * context)
{
    String * result = String::string();
    for(unsigned int i = 0 ; i < part->parts()->count() ; i ++) {
        AbstractPart * subpart = (AbstractPart *) part->parts()->objectAtIndex(i);
        String * substring = htmlForAbstractPart(subpart, context);
        if (context->pass != 0 && substring != NULL) {
            result->appendString(substring);
        }
    }
    return  result;
}

static String * htmlForAbstractMultipartRelated(AbstractMultipart * part, htmlRendererContext * context)
{
    if (part->parts()->count() == 0) {
        if (context->pass == 0) {
            return NULL;
        }
        else {
            return MCSTR("");
        }
    }
    
    // root of the multipart/related.
    AbstractPart * subpart = (AbstractPart *) part->parts()->objectAtIndex(0);
    if (context->relatedAttachments != NULL) {
        for(unsigned int i = 1 ; i < part->parts()->count() ; i ++) {
            AbstractPart * otherSubpart = (AbstractPart *) part->parts()->objectAtIndex(i);
            if (context->relatedAttachments != NULL) {
                context->relatedAttachments->addObject(otherSubpart);
            }
        }
    }
    return htmlForAbstractPart(subpart, context);
}

static void fillTemplateDictionaryFromMCHashMap(ctemplate::TemplateDictionary * dict, HashMap * mcHashMap)
{
    Array * keys = mcHashMap->allKeys();
    
    for(unsigned int i = 0 ; i < keys->count() ; i ++) {
        String * key = (String *) keys->objectAtIndex(i);
        Object * value;
        
        value = mcHashMap->objectForKey(key);
        if (value->className()->isEqual(MCSTR("mailcore::String"))) {
            String * str;
            
            str = (String *) value;
            dict->SetValue(key->UTF8Characters(), str->UTF8Characters());
        }
        else if (value->className()->isEqual(MCSTR("mailcore::Array"))) {
            Array * array;
            
            array = (Array *) value;
            for(unsigned int k = 0 ; k < array->count() ; k ++) {
                HashMap * item = (HashMap *) array->objectAtIndex(k);
                ctemplate::TemplateDictionary * subDict = dict->AddSectionDictionary(key->UTF8Characters());
                fillTemplateDictionaryFromMCHashMap(subDict, item);
            }
        }
        else if (value->className()->isEqual(MCSTR("mailcore::HashMap"))) {
            ctemplate::TemplateDictionary * subDict;
            HashMap * item;
            
            item = (HashMap *) value;
            subDict = dict->AddSectionDictionary(key->UTF8Characters());
            fillTemplateDictionaryFromMCHashMap(subDict, item);
        }
    }
}

static String * renderTemplate(String * templateContent, HashMap * values)
{
    ctemplate::TemplateDictionary dict("template dict");
    std::string output;
    Data * data;
    
    fillTemplateDictionaryFromMCHashMap(&dict, values);
    data = templateContent->dataUsingEncoding("utf-8");
    ctemplate::Template * tpl = ctemplate::Template::StringToTemplate(data->bytes(), data->length(), ctemplate::DO_NOT_STRIP);
    if (tpl == NULL)
        return NULL;
    if (!tpl->Expand(&output, &dict))
        return NULL;
    delete tpl;
    
    return String::stringWithUTF8Characters(output.c_str());
}

String * PlainTextRenderer::htmlForRFC822Message(MessageParser * message,
                                            HTMLRendererTemplateCallback * htmlCallback)
{
    return htmlForAbstractMessage(NULL, message, NULL, htmlCallback, NULL, NULL);
}

String * PlainTextRenderer::htmlForIMAPMessage(String * folder,
                                          IMAPMessage * message,
                                          HTMLRendererIMAPCallback * dataCallback,
                                          HTMLRendererTemplateCallback * htmlCallback)
{
    return htmlForAbstractMessage(folder, message, dataCallback, htmlCallback, NULL, NULL);
}

Array * PlainTextRenderer::attachmentsForMessage(AbstractMessage * message)
{
    Array * attachments = Array::array();
    HTMLRendererIMAPCallback * dataCallback = new HTMLRendererIMAPDummyCallback();
    String * ignoredResult = htmlForAbstractMessage(NULL, message, dataCallback, NULL, attachments, NULL);
    delete dataCallback;
    dataCallback = NULL;
    (void) ignoredResult; // remove unused variable warning.
    return attachments;
}

Array * PlainTextRenderer::htmlInlineAttachmentsForMessage(AbstractMessage * message)
{
    Array * htmlInlineAttachments = Array::array();
    HTMLRendererIMAPCallback * dataCallback = new HTMLRendererIMAPDummyCallback();
    String * ignoredResult = htmlForAbstractMessage(NULL, message, dataCallback, NULL, NULL, htmlInlineAttachments);
    delete dataCallback;
    dataCallback = NULL;
    (void) ignoredResult; // remove unused variable warning.
    return htmlInlineAttachments;
}

Array * PlainTextRenderer::requiredPartsForRendering(AbstractMessage * message)
{
    HTMLRendererIMAPDummyCallback * dataCallback = new HTMLRendererIMAPDummyCallback();
    String * ignoredResult = htmlForAbstractMessage(NULL, message, dataCallback, NULL, NULL, NULL);
    
    Array *requiredParts = dataCallback->requiredParts();
    
    delete dataCallback;
    dataCallback = NULL;
    (void) ignoredResult; // remove unused variable warning.
    return requiredParts;
}
