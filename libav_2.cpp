

//sudo apt install libavformat-dev libavcodec-dev libavutil-dev

// This version is for the 0.4.9+ release of ffmpeg. This release adds the
// av_read_frame() API call, which simplifies the reading of video frames 
// considerably.
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <iostream>



/** BMP writer.*/
#define DATA_OFFSET_OFFSET 0x000A
#define WIDTH_OFFSET 0x0012
#define HEIGHT_OFFSET 0x0016
#define BITS_PER_PIXEL_OFFSET 0x001C
#define HEADER_SIZE 14
#define INFO_HEADER_SIZE 40
#define NO_COMPRESION 0
#define MAX_NUMBER_OF_COLORS 0
#define ALL_COLORS_REQUIRED 0
typedef unsigned int int32;
typedef short int16;
typedef unsigned char byte;
void WriteImage(const char *fileName, byte *pixels, int32 width, int32 height,int32 bytesPerPixel)
{
	FILE *outputFile = fopen(fileName, "wb");
	//*****HEADER************//
	const char *BM = "BM";
	fwrite(&BM[0], 1, 1, outputFile);
	fwrite(&BM[1], 1, 1, outputFile);
	int paddedRowSize = (int)(4 * ceil((float)width/4.0f))*bytesPerPixel;
	int32 fileSize = paddedRowSize*height + HEADER_SIZE + INFO_HEADER_SIZE;
	fwrite(&fileSize, 4, 1, outputFile);
	int32 reserved = 0x0000;
	fwrite(&reserved, 4, 1, outputFile);
	int32 dataOffset = HEADER_SIZE+INFO_HEADER_SIZE;
	fwrite(&dataOffset, 4, 1, outputFile);

	//*******INFO*HEADER******//
	int32 infoHeaderSize = INFO_HEADER_SIZE;
	fwrite(&infoHeaderSize, 4, 1, outputFile);
	fwrite(&width, 4, 1, outputFile);
	fwrite(&height, 4, 1, outputFile);
	int16 planes = 1; //always 1
	fwrite(&planes, 2, 1, outputFile);
	int16 bitsPerPixel = bytesPerPixel * 8;
	fwrite(&bitsPerPixel, 2, 1, outputFile);
	//write compression
	int32 compression = NO_COMPRESION;
	fwrite(&compression, 4, 1, outputFile);
	//write image size (in bytes)
	int32 imageSize = width*height*bytesPerPixel;
	fwrite(&imageSize, 4, 1, outputFile);
	int32 resolutionX = 11811; //300 dpi
	int32 resolutionY = 11811; //300 dpi
	fwrite(&resolutionX, 4, 1, outputFile);
	fwrite(&resolutionY, 4, 1, outputFile);
	int32 colorsUsed = MAX_NUMBER_OF_COLORS;
	fwrite(&colorsUsed, 4, 1, outputFile);
	int32 importantColors = ALL_COLORS_REQUIRED;
	fwrite(&importantColors, 4, 1, outputFile);
	int unpaddedRowSize = width*bytesPerPixel;
	for ( decltype(height) i = 0; i < height; i++ )
	{
	        int pixelOffset = ((height - i) - 1)*unpaddedRowSize;
	        fwrite(&pixels[pixelOffset], 1, paddedRowSize, outputFile); 
	}
	fclose(outputFile);
}



const char * VIDEO_FILE = "video.mkv";
const auto METHOD = 3;

using namespace std;

int main()
{
    av_register_all();
    avformat_network_init();
    avfilter_register_all();

    //crashes on -Ofast without =NULL initialization:
    AVFormatContext * format = NULL;
    if ( avformat_open_input( & format, VIDEO_FILE, NULL, NULL ) != 0 ) {
        cerr << "Could not open file " << VIDEO_FILE << endl;
        return -1;
    }

    // Retrieve stream information
    if ( avformat_find_stream_info( format, NULL ) < 0) {
        cerr << "avformat_find_stream_info() failed." << endl;
        return -1;
    }
    av_dump_format( format, 0, VIDEO_FILE, false );
    
    AVCodec * video_dec = (AVCodec*)1;
    AVCodec * audio_dec = (AVCodec*)1;
    const auto video_stream_index = av_find_best_stream( format, AVMEDIA_TYPE_VIDEO, -1, -1, & video_dec, 0 );
    const auto audio_stream_index = av_find_best_stream( format, AVMEDIA_TYPE_AUDIO, -1, -1, & audio_dec, 0 );
    if ( video_stream_index < 0 ) {
        cerr << "Failed to find video stream." << endl;
        return -1;
    }
    if ( audio_stream_index < 0 ) {
        cerr << "Failed to find audio stream." << endl;
        return -1;
    }

    AVCodecParameters * videoParams = format->streams[ video_stream_index ]->codecpar;
    cout << "Having " << videoParams->width << " | " << videoParams->height << " video." << endl;
    
    av_read_play( format );

    // create decoding context
    AVCodecContext * video_ctx = avcodec_alloc_context3( video_dec );
    AVCodecContext * audio_ctx = avcodec_alloc_context3( audio_dec );
    if ( ! video_ctx || ! audio_ctx ) {
        cerr << "Failed to avcodec_alloc_context3()" << endl;
        return -1;
    }
    if ( video_dec->capabilities & AV_CODEC_CAP_TRUNCATED ) video_ctx->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames
    
    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */
  
    avcodec_parameters_to_context( video_ctx, format->streams[ video_stream_index ]->codecpar );
    avcodec_parameters_to_context( audio_ctx, format->streams[ audio_stream_index ]->codecpar );
    if ( avcodec_open2( video_ctx, video_dec, NULL ) < 0 ) {
        cout << "Failed to open video decoder." << endl;
        return -1;
    }
    if ( avcodec_open2( audio_ctx, audio_dec, NULL ) < 0 ) {
        cout << "Failed to open audio decoder." << endl;
        return -1;
    }
    
    AVFrame* pic_src = av_frame_alloc();
    AVFrame* pic_out = av_frame_alloc();
    
    const auto W = videoParams->width;
    const auto H = videoParams->height;
    //const auto SRC_FORMAT = video_ctx->pix_fmt;
    const auto SRC_FORMAT = (AVPixelFormat)videoParams->format;
    //const auto SRC_FORMAT = AV_PIX_FMT_YUV420P;
    cout << W << " | " << H << " | " << SRC_FORMAT << endl;
    
    const auto OUT_FORMAT = AV_PIX_FMT_RGB24;
    const auto ALIGN = 32;
    
    //succeeds but sws_scale() complains about "[swscaler @ 0x55a97f5615c0] bad src image pointers" and just copies pic_src to pic_out unchanged:
    if      ( METHOD == 1 ) {
        uint8_t* src_buffer = (uint8_t*)av_malloc( avpicture_get_size( SRC_FORMAT, W, H ) );
        uint8_t* out_buffer = (uint8_t*)av_malloc( avpicture_get_size( OUT_FORMAT, W, H ) );
        avpicture_fill( (AVPicture *) pic_src, src_buffer, SRC_FORMAT, W, H );
        avpicture_fill( (AVPicture *) pic_out, out_buffer, OUT_FORMAT, W, H );
    }
    else if ( METHOD == 2 ) {
        uint8_t* src_buffer = (uint8_t*)av_malloc( av_image_get_buffer_size( SRC_FORMAT, W, H, ALIGN ) );
        uint8_t* out_buffer = (uint8_t*)av_malloc( av_image_get_buffer_size( OUT_FORMAT, W, H, ALIGN ) );
        av_image_fill_arrays( pic_src->data, pic_src->linesize, NULL, SRC_FORMAT, W, H, ALIGN );
        av_image_fill_arrays( pic_out->data, pic_out->linesize, NULL, OUT_FORMAT, W, H, ALIGN );
    }
    //fails with "Failed to allocate av image" error:
    else if ( METHOD == 3 ) {
        pic_src->data[0] = NULL;
        pic_out->data[0] = NULL;
        pic_src->width  = W;
        pic_src->height = H;
        pic_out->width  = W;
        pic_out->height = H;
        pic_src->format = SRC_FORMAT;
        pic_out->format = OUT_FORMAT;
        if (
            av_image_alloc( pic_src->data, pic_src->linesize, W, H, SRC_FORMAT, ALIGN ) != 0
            ||
            av_image_alloc( pic_out->data, pic_out->linesize, W, H, OUT_FORMAT, ALIGN ) != 0
        ) {
            cerr << "Failed to allocate av image." << endl;
            return -1;
        }
    }
    //fails with "Failed to allocate av frame." error:
    else if ( METHOD == 4 ) {
        if ( 
            av_frame_get_buffer( pic_src, ALIGN ) != 0
            ||
            av_frame_get_buffer( pic_out, ALIGN ) != 0
        ) {
            cerr << "Failed to allocate av frame." << endl;
            return -1;
        }
    }
    
    struct SwsContext * img_convert_ctx = sws_getContext(
        W, H, SRC_FORMAT,
        W, H, OUT_FORMAT,
        SWS_BICUBIC,
        NULL, NULL, NULL
    );
    if ( img_convert_ctx == NULL ) {
        cerr << "Could NOT initialize the conversion context!" << endl;
        return -1;
    }
    
    AVPacket packet;
    av_init_packet( & packet );
    
    //for first 10 frames only:
    int cnt = 0;
    while ( av_read_frame( format, & packet ) >= 0 && cnt < 10 ) {
        if ( packet.stream_index == video_stream_index ) {
            int check;
            const auto result = avcodec_decode_video2( video_ctx, pic_src, & check, & packet );
            cout << "Bytes decoded " << result << " check " << check << endl;
            
			const auto ret = sws_scale(
                img_convert_ctx,
                pic_src->data,
                pic_src->linesize,
                0, 
				H,
                pic_out->data,
                pic_out->linesize
            );
            
            std::string name = "./";
            name += std::to_string( cnt ) + ".bmp";
            cout << "Writing frame " << name << " with linesize " << pic_out->linesize[0] << " ..." << endl;
            WriteImage( name.c_str(), (uint8_t*) pic_out->data, W, H, 3 );
            
            if ( ret != H ) {
                cerr << "sws_scale() worked out unexpectedly." << endl;
                return -1;
            }
            
            av_frame_unref( pic_src );
            
            ++ cnt;
        }
        else if ( packet.stream_index == audio_stream_index ) {
            cout << "Sound packet" << endl;
        }
        av_free_packet( & packet );
        av_init_packet( & packet );
    }

    return 0;
}