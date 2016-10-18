//  Created by Chris Harding on 23/04/2012.
//  Copyright (c) 2012 Swift Navigation. All rights reserved.
//

#import "OpenGLProcessor.h"
#import "OffscreenFBO.h"

#include "Shader.h"
#include "GLConstants.h"
#include "GLUtils.h"

@interface OpenGLProcessor ()
{
    float mMaxAnisotropy;
}

@end

@implementation OpenGLProcessor

@synthesize zoomFactor;

- (id)init
{
    if(self = [super init])
    {
        zoomFactor = 1.0;
        
        if(![self createContext]) NSLog(@"Problem setting up context");
        if(![self generateTextureCaches]) NSLog(@"Problem generating texture cache");
        
        yuv2yuvProgram = new Shader("yuv2yuv.vert", "yuv2yuv.frag");
        yuv2yuvProgram->compile();
#ifndef USE_SINGLE_PASS_PREPROCESS
        yPlanarProgram = new Shader("y2planar.vert", "y2planar.frag");
        yPlanarProgram->compile();
        uPlanarProgram = new Shader("u2planar.vert", "u2planar.frag");
        uPlanarProgram->compile();
        vPlanarProgram = new Shader("v2planar.vert", "v2planar.frag");
        vPlanarProgram->compile();
#endif 
        inputTexture[0] = NULL;
        inputTexture[1] = NULL;
        
        isFirstRenderCall = YES;
    }

    return self;
}

- (void)dealloc
{
    NSLog(@"VideoProcessor exiting");
    
    delete yuv2yuvProgram;
#ifndef USE_SINGLE_PASS_PREPROCESS
    delete yPlanarProgram;
    delete uPlanarProgram;
    delete vPlanarProgram;
#endif 
}

- (void)setOutputWidth:(int)width height:(int)height
{
    // We set the output dimensions at a nice iPhone/iPad friendly aspect ratio
    outputPixelBufferWidth = width;
    outputPixelBufferHeight = height;
    NSLog(@"Output pixel buffer dimensions %zu x %zu", outputPixelBufferWidth, outputPixelBufferHeight);
}

- (void)processVideoFrameYuv:(CVPixelBufferRef)inputPixelBuffer
{
    // This isn't the only OpenGL ES context
    [EAGLContext setCurrentContext:oglContext];
    
    // Large amount of setup is done on the first call to render, when the pixel buffer dimensions are available
    if(isFirstRenderCall)
    {
        // Record the dimensions of the pixel buffer (we assume they won't change from now on)
        inputPixelBufferWidth = CVPixelBufferGetWidth(inputPixelBuffer);
        inputPixelBufferHeight = CVPixelBufferGetHeight(inputPixelBuffer);
        NSLog(@"Input pixel buffer dimensions %zu x %zu", inputPixelBufferWidth, inputPixelBufferHeight);
        
        // These calls use the pixel buffer dimensions
        [self createPixelBuffer:&outputPixelBuffer width:outputPixelBufferWidth height:outputPixelBufferHeight];
        
        // We can now create the output texture, which only need to be done once (whereas the input texture must be created once per frame)
        outputTexture = [self createTextureLinkedToBuffer:outputPixelBuffer withCache:outputTextureCache textureType:TT_RGBA];
        
        // With their respective textures we can now create the two offscreen buffers
        offscreenFrameBuffer[0] = [[OffscreenFBO alloc] initWithTexture:outputTexture 
                                                                  width:(int)outputPixelBufferWidth
                                                                 height:(int)outputPixelBufferHeight];//*/
        
        // Create intermediate texture with resize data
        offscreenFrameBuffer[1] = [[OffscreenFBO alloc] initWithWidth:(int)outputPixelBufferWidth
                                                               height:(int)outputPixelBufferHeight];
        
        // setup shader params
        yuv2yuvProgram->bind();
        yuv2yuvProgram->setInt1("yPlane", 0);
        yuv2yuvProgram->setInt1("uvPlane", 1);
        
#ifndef USE_SINGLE_PASS_PREPROCESS
        // setup YUV params
        GLfloat resultYSize[] = { outputPixelBufferWidth - 1, outputPixelBufferHeight - 1, outputPixelBufferWidth, 0.0f };
        GLfloat resultYInvSize[] = { 1.f / resultYSize[0], 1.f / resultYSize[1], 1.f / resultYSize[2] };
        GLfloat resultUVSize[] = { outputPixelBufferWidth / 2 - 1, outputPixelBufferHeight / 2 - 1, outputPixelBufferWidth / 2, 0.0f };
        GLfloat resultUVInvSize[] = { 1.f / resultUVSize[0], 1.f / resultUVSize[1], 1.f / resultUVSize[2] };
        
        yPlanarProgram->bind();
        yPlanarProgram->setFloatN("resultSize", resultYSize, 3);
        yPlanarProgram->setFloatN("resultInvSize", resultYInvSize, 3);
        
        uPlanarProgram->bind();
        uPlanarProgram->setFloatN("resultSize", resultUVSize, 3);
        uPlanarProgram->setFloatN("resultInvSize", resultUVInvSize, 3);
        uPlanarProgram->setFloatN("planeSize", resultYSize, 3);
        
        vPlanarProgram->bind();
        vPlanarProgram->setFloatN("resultSize", resultUVSize, 3);
        vPlanarProgram->setFloatN("resultInvSize", resultUVInvSize, 3);
        vPlanarProgram->setFloatN("planeSize", resultYSize, 3);
#endif 
        isFirstRenderCall = NO;
    }
    
    // Pass 1: resizing
#ifdef USE_SINGLE_PASS_PREPROCESS
    [offscreenFrameBuffer[0] beginRender];
#else 
    // render to intermediate buffer
    [offscreenFrameBuffer[1] beginRender];
#endif
    //glClear(GL_COLOR_BUFFER_BIT);
    
    // We also need to recalculate texture vertices in case the zoom level has changed
    GL::calculateUVs(GL::CM_SCALE_ASPECT_TO_FILL, inputPixelBufferWidth / (float)inputPixelBufferHeight, 
                     outputPixelBufferWidth / (float)outputPixelBufferHeight, zoomFactor, cropInputTextureVertices);//*/
    
    // We should lock/unlock input pixel buffer to prevent strange artifacts 
    CVPixelBufferLockBaseAddress(inputPixelBuffer, 0);
    // Bind the input texture containing a new video frame, ensuring the texture vertices crop the input
    glActiveTexture(GL_TEXTURE0);
    inputTexture[0] = [self createTextureLinkedToBuffer:inputPixelBuffer withCache:inputTextureCache textureType:TT_LUMA];
    glActiveTexture(GL_TEXTURE1);
    inputTexture[1] = [self createTextureLinkedToBuffer:inputPixelBuffer withCache:inputTextureCache textureType:TT_CHROMA];//*/
    CVPixelBufferUnlockBaseAddress(inputPixelBuffer, 0);
    
    glEnableVertexAttribArray(SA_POSITION);
    glEnableVertexAttribArray(SA_TEXTURE0);
    glVertexAttribPointer(SA_POSITION, 2, GL_FLOAT, 0, 0, GL::originCentredSquareVertices);
    glVertexAttribPointer(SA_TEXTURE0, 2, GL_FLOAT, 0, 0, cropInputTextureVertices);
    
    // Render video frame offscreen to the FBO
    yuv2yuvProgram->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Cleanup input texture
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0); // unbind
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0); // unbind
    //
    if(inputTexture[0]) CFRelease(inputTexture[0]), inputTexture[0] = NULL;
    if(inputTexture[1]) CFRelease(inputTexture[1]), inputTexture[1] = NULL;
    CVOpenGLESTextureCacheFlush(inputTextureCache, 0);//*/

#ifdef USE_SINGLE_PASS_PREPROCESS
    [offscreenFrameBuffer[0] endRender];
#else
    [offscreenFrameBuffer[1] endRender];

    // Pass 2: render YUV
    [offscreenFrameBuffer[0] beginRender];
    [offscreenFrameBuffer[1] bindTexture];
    //glClear(GL_COLOR_BUFFER_BIT);
    
    // render Y
    glVertexAttribPointer(SA_POSITION, 2, GL_FLOAT, 0, 0, GL::yPlaneVertices);
    glVertexAttribPointer(SA_TEXTURE0, 2, GL_FLOAT, 0, 0, GL::yPlaneUVs);
    
    yPlanarProgram->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    //render U
    glVertexAttribPointer(SA_POSITION, 2, GL_FLOAT, 0, 0, GL::uPlaneVertices);
    glVertexAttribPointer(SA_TEXTURE0, 2, GL_FLOAT, 0, 0, GL::uvPlaneUVs);
    
    uPlanarProgram->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    //render V
    glVertexAttribPointer(SA_POSITION, 2, GL_FLOAT, 0, 0, GL::vPlaneVertices);
    glVertexAttribPointer(SA_TEXTURE0, 2, GL_FLOAT, 0, 0, GL::uvPlaneUVs);
    
    vPlanarProgram->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vPlanarProgram->unbind();
    
    glBindTexture(GL_TEXTURE_2D, 0); // unbind
    [offscreenFrameBuffer[0] endRender];
#endif
    
    // Process using delegate
    [self.delegate didProcessFrame:outputPixelBuffer];
    
    // Cleanup output texture (but do not release)
    CVOpenGLESTextureCacheFlush(outputTextureCache, 0);
}

#pragma mark
#pragma mark Primary initialisation helper methods

- (Boolean)createContext
{
    // Create the graphics context
    oglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    if(!oglContext || ![EAGLContext setCurrentContext:oglContext])
        return false;
        
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    //glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    
    mMaxAnisotropy = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &mMaxAnisotropy);
    
    return true;
}

- (Boolean)generateTextureCaches
{
    CVReturn err;
    
     //  Create a new video input texture cache
    err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, (__bridge CVEAGLContext)((__bridge void *)(oglContext)), NULL, &inputTextureCache);
    if(err)
    {
        NSLog(@"Error creating input texture cache with CVReturn error %u", err);
        return false;
    }
    
    //  Create a new video output texture cache
    err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, (__bridge CVEAGLContext)((__bridge void *)(oglContext)), NULL, &outputTextureCache);
    if(err)
    {
        NSLog(@"Error creating output texture cache with CVReturn error %u", err);
        return false;
    }
    
    if(inputTextureCache == NULL || outputTextureCache == NULL)
    {
        NSLog(@"One or more texture caches are null");
        return false; 
    }
    
    return true;
}

#pragma mark 
#pragma mark Secondary initialisation helper methods
// Performed on reciept of the first frame

- (Boolean)createPixelBuffer:(CVPixelBufferRef*)pixelBufferPtr width:(size_t)width height:(size_t)height
{
    // Define the output pixel buffer attibutes
    CFDictionaryRef emptyValue = CFDictionaryCreate(kCFAllocatorDefault, // our empty IOSurface properties dictionary
                                                    NULL,
                                                    NULL,
                                                    0,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CFMutableDictionaryRef pixelBufferAttributes = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                             1,
                                                                             &kCFTypeDictionaryKeyCallBacks,
                                                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(pixelBufferAttributes, kCVPixelBufferIOSurfacePropertiesKey, emptyValue);
    
    // Create the pixel buffer
    CVReturn err = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                                       kCVPixelFormatType_32BGRA,
                                       pixelBufferAttributes,
                                       pixelBufferPtr);
    CFRelease(emptyValue);
    CFRelease(pixelBufferAttributes);
    
    // Check for success
    if(err)
    {
        NSLog(@"Error creating output pixel buffer with CVReturn error %u", err);
        return false;
    }
    
    return true;
}

#pragma mark 
#pragma mark Texture creation

- (CVOpenGLESTextureRef)createTextureLinkedToBuffer:(CVPixelBufferRef)pixelBuffer
                                          withCache:(CVOpenGLESTextureCacheRef)textureCache
                                        textureType:(int)textureType
{
    size_t planeIndex = (textureType == TT_CHROMA) ? 1 : 0;
    GLint format = (textureType == TT_RGBA) ? GL_RGBA :
                        (textureType == TT_LUMA) ? GL_LUMINANCE : GL_LUMINANCE_ALPHA;
    unsigned int width = (unsigned int)(CVPixelBufferGetWidth(pixelBuffer) >> planeIndex);
    unsigned int height = (unsigned int)(CVPixelBufferGetHeight(pixelBuffer) >> planeIndex);
    CVOpenGLESTextureRef texture = NULL;
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault, 
                                                                textureCache,
                                                                pixelBuffer,
                                                                NULL,
                                                                GL_TEXTURE_2D,
                                                                format,
                                                                width,
                                                                height,
                                                                format,
                                                                GL_UNSIGNED_BYTE,
                                                                planeIndex,
                                                                &texture);

    
    if(!texture || err)
    {
        NSLog(@"CVOpenGLESTextureCacheCreateTextureFromImage failed (error: %d)", err);  
    }
    
    // Set texture parameters
    glBindTexture(CVOpenGLESTextureGetTarget(texture), CVOpenGLESTextureGetName(texture));
    
    if(mMaxAnisotropy > 0.0f)
    {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, mMaxAnisotropy);
    }
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    return texture;
}

@end
