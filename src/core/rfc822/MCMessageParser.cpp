#include "MCMessageParser.h"

#include <libetpan/libetpan.h>
#if __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "MCDefines.h"
#include "MCAttachment.h"
#include "MCMultipart.h"
#include "MCMessageHeader.h"
#include "MCHTMLRenderer.h"
#include "MCPlainTextRenderer.h"
#include "MCHTMLBodyRendererTemplateCallback.h"

using namespace mailcore;

MessageParser * MessageParser::messageParserWithData(Data * data)
{
    MessageParser * parser = new MessageParser(data);
    return (MessageParser *) parser->autorelease();
}

MessageParser * MessageParser::messageParserWithContentsOfFile(String * filename)
{
    Data * data = Data::dataWithContentsOfFile(filename);
    return messageParserWithData(data);
}

void MessageParser::init()
{
    mData = NULL;
    mMainPart = NULL;
#if __APPLE__
    mNSData = NULL;
#endif
}

void MessageParser::setBytes(char * dataBytes, unsigned int dataLength)
{
    const char * start = NULL;
    unsigned int length = 0;
    if (dataLength > 5) {
        if (strncmp(dataBytes, "From ", 5) == 0) {
            start = dataBytes;
            for(unsigned int i = 0 ; i < dataLength ; i ++) {
                if (start[i] == '\n') {
                    start = start + i + 1;
                    length = dataLength - (i + 1);
                    break;
                }
            }
        }
    }
    if (start != NULL) {
        dataBytes = (char *) start;
        dataLength = length;
    }
    
    mailmessage * msg;
    struct mailmime * mime;
    
    msg = data_message_init(dataBytes, dataLength);
    mailmessage_get_bodystructure(msg, &mime);
    mMainPart = (AbstractPart *) Attachment::attachmentsWithMIME(msg->msg_mime)->retain();
    mMainPart->applyUniquePartID();
    
    size_t cur_token = 0;
    struct mailimf_fields * fields;
    int r = mailimf_envelope_and_optional_fields_parse(dataBytes, dataLength, &cur_token, &fields);
    if (r == MAILIMAP_NO_ERROR) {
        header()->importIMFFields(fields);
        mailimf_fields_free(fields);
    }
    mailmessage_free(msg);
}

MessageParser::MessageParser()
{
    init();
}

MessageParser::MessageParser(Data * data)
{
    init();
    
    setBytes(data->bytes(), data->length());
    mData = (Data *) data->retain();
}

MessageParser::MessageParser(MessageParser * other) : AbstractMessage(other)
{
    init();
    MC_SAFE_REPLACE_RETAIN(Data, mData, other->mData);
    MC_SAFE_REPLACE_RETAIN(AbstractPart, mMainPart, other->mMainPart);
}

MessageParser::~MessageParser()
{
    MC_SAFE_RELEASE(mMainPart);
    MC_SAFE_RELEASE(mData);
#if __APPLE__
    if (mNSData != NULL) {
        CFRelease(mNSData);
    }
#endif
}

AbstractPart * MessageParser::mainPart()
{
    return mMainPart;
}

Data * MessageParser::data()
{
#if __APPLE__
    if (mNSData != NULL) {
        return dataFromNSData();
    }
#endif
    return mData;
}

String * MessageParser::description()
{
    String * result = String::string();
    result->appendUTF8Format("<%s:%p ", MCUTF8(className()), this);
    result->appendUTF8Format("<%p>", mMainPart);
    if (mMainPart != NULL) {
        result->appendString(mMainPart->description());
    }
    result->appendUTF8Characters(">");
    
    return result;
}

HashMap * MessageParser::serializable()
{
    HashMap * result = AbstractMessage::serializable();
    if (mMainPart != NULL) {
        result->setObjectForKey(MCSTR("mainPart"), mMainPart->serializable());
    }
    return result;
}

void MessageParser::importSerializable(HashMap * serializable)
{
    AbstractMessage::importSerializable(serializable);
    MC_SAFE_REPLACE_RETAIN(AbstractPart, mMainPart, Object::objectWithSerializable((HashMap *) serializable->objectForKey(MCSTR("mainPart"))));
    if (mMainPart != NULL) {
        mMainPart->applyUniquePartID();
    }
}

Object * MessageParser::copy()
{
    return new MessageParser(this);
}

AbstractPart * MessageParser::partForContentID(String * contentID)
{
    if (mainPart() == NULL) {
        return NULL;
    }
    return mainPart()->partForContentID(contentID);
}

AbstractPart * MessageParser::partForUniqueID(String * uniqueID)
{
    if (mainPart() == NULL) {
        return NULL;
    }
    return mainPart()->partForUniqueID(uniqueID);
}

String * MessageParser::htmlRendering(HTMLRendererTemplateCallback * htmlCallback)
{
    return HTMLRenderer::htmlForRFC822Message(this, htmlCallback);
}

String * MessageParser::htmlBodyRendering()
{
    HTMLBodyRendererTemplateCallback * callback = new HTMLBodyRendererTemplateCallback();
    String * result = htmlRendering(callback);
    MC_SAFE_RELEASE(callback);
    return result;
}

String * MessageParser::plainTextRendering()
{
    String * html = PlainTextRenderer::htmlForRFC822Message(this, NULL);
    return html;
}

String * MessageParser::plainTextBodyRendering(bool stripWhitespace)
{
    String * plainTextBodyString = plainTextRendering();
    
    if (stripWhitespace && plainTextBodyString != NULL) {
        plainTextBodyString = plainTextBodyString->stripWhitespace();
    }
    return plainTextBodyString;
}

static void * createObject()
{
    return new MessageParser();
}

INITIALIZE(MessageParser)
{
    Object::registerObjectConstructor("mailcore::MessageParser", &createObject);
}
