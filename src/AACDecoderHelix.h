#pragma once

#include "Arduino.h"
#include "CommonHelix.h"
#include "libhelix-aac/aacdec.h"

#define AAC_MAX_OUTPUT_SIZE 2048 
#define AAC_MAX_FRAME_SIZE 1600 

namespace libhelix {

typedef void (*AACInfoCallback)(_AACFrameInfo &info);
typedef void (*AACDataCallback)(_AACFrameInfo &info,short *pwm_buffer, size_t len);

/**
 * @brief A simple Arduino API for the libhelix AAC decoder. The data us provided with the help of write() calls.
 * The decoded result is available either via a callback method or via an output stream.
 */
class AACDecoderHelix : public CommonHelix {
    public:
        AACDecoderHelix() {
        }

        AACDecoderHelix(Print &output, AACInfoCallback infoCallback=nullptr){
            this->out = &output;
            this->infoCallback = infoCallback;
        }

        AACDecoderHelix(AACDataCallback dataCallback){
            this->pwmCallback = dataCallback;
        }
        void setInfoCallback(AACInfoCallback cb){
            this->infoCallback = cb;
        }

        void setDataCallback(AACDataCallback cb){
            this->pwmCallback = cb;
        }

        void setOutput(Print &output){
            this->out = &output;
        }

         /// Starts the processing
        void begin(){
            LOG(Debug, "begin");
            CommonHelix::begin();
            decoder = AACInitDecoder();
            if (decoder==nullptr){
                LOG(Error, "MP3InitDecoder has failed");
                active = false;
                return;
            }
        }

        /// Releases the reserved memory
        void end(){
            LOG(Debug, "end");
            AACFreeDecoder(decoder);
            CommonHelix::end();
        }

        /// Provides the last available _AACFrameInfo_t
        _AACFrameInfo audioInfo(){
            return aacFrameInfo;
        }

    protected:
        HAACDecoder decoder = nullptr;
        AACDataCallback pwmCallback = nullptr;
        AACInfoCallback infoCallback = nullptr;
        _AACFrameInfo aacFrameInfo;

        size_t maxFrameSize(){
            return max_frame_size == 0 ? AAC_MAX_FRAME_SIZE : max_frame_size;
        }

        size_t maxPWMSize() {
            return max_pwm_size == 0 ? AAC_MAX_OUTPUT_SIZE : max_pwm_size;
        }

        int findSynchWord(int offset=0) {
            int result = AACFindSyncWord(frame_buffer+offset, buffer_size)+offset;
            return result < 0 ? result : result + offset;
        }

        /// decods the data and removes the decoded frame from the buffer
        void decode(Range r) {
            LOG(Debug, "decode %d", r.end);
            int len = r.end;
            int bytesLeft =  r.end; //r.end; //r.end; // buffer_size
            uint8_t* ptr = frame_buffer + r.start;

            int result = AACDecode(decoder, &ptr, &bytesLeft, pwm_buffer);
            int decoded = len - bytesLeft;
            assert(decoded == ptr-(frame_buffer + r.start));
            if (result==0){
                LOG(Debug, "-> bytesLeft %d -> %d  = %d ", buffer_size, bytesLeft, decoded);
                LOG(Debug, "-> End of frame (%d) vs end of decoding (%d)", r.end, decoded)

                // return the decoded result
                _AACFrameInfo info;
                AACGetLastFrameInfo(decoder, &info);
                provideResult(info);

                // remove processed data from buffer 
                buffer_size -= decoded;
                assert(buffer_size<=maxFrameSize());
                memmove(frame_buffer, frame_buffer+r.start+decoded, buffer_size);
                LOG(Debug, " -> decoded %d bytes - remaining buffer_size: %d", decoded, buffer_size);
            } else {
                // in the case of error we remove the frame
                LOG(Debug, " -> decode error: %d - removing frame!", result);
                int ignore = decoded;
                if (ignore <= 0) ignore = r.end;
                // We advance to the next synch world
                buffer_size -= ignore;
                assert(buffer_size<=maxFrameSize());
                memmove(frame_buffer, frame_buffer+ignore, buffer_size);
            }
        }

        // return the result PWM data
        void provideResult(_AACFrameInfo &info){
            LOG(Debug, "provideResult: %d samples",info.outputSamps);
             if (info.outputSamps>0){
            // provide result
                if(pwmCallback!=nullptr){
                    // output via callback
                    pwmCallback(info, pwm_buffer,info.outputSamps);
                } else {
                    // output to stream
                    if (info.sampRateOut!=aacFrameInfo.sampRateOut && infoCallback!=nullptr){
                        infoCallback(info);
                    }
                    out->write((uint8_t*)pwm_buffer, info.outputSamps);
                }
                aacFrameInfo = info;
            }
        }            
};
}