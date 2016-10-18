//  Created by Chris Harding on 31/12/2011.
//  Copyright (c) 2011 Swift Navigation. All rights reserved.
//
//  Creates and initialises the various submodules - linking them with each other and with the networking module. Initialised with a session and a delegate to handle connection start/finish.

#import <UIKit/UIKit.h>
#import "GalileoCommon.h"

@class GKSessionManager;
@class GKNetController;
@class AudioInputOutput;
@class VideoInputOutput;
@class VideoViewController;
@class UserInputHandler;
@class DockConnectorController;
@class MediaOutput;
@class CameraInput;

@interface RemoteControlGalileo : UIViewController
{
    NSTimer *pingPingTimer;
    
    id<NetworkControllerDelegate> networkController;

    MediaOutput *mediaOutput;
    CameraInput *cameraInput;
    VideoInputOutput *videoInputOutput;
    AudioInputOutput *audioInputOutput;
    VideoViewController *videoViewController;
    UserInputHandler *userInputHandler;
    DockConnectorController *serialController;
}

// The video view has to be an accessible property so that it can be displayed
@property(readonly, nonatomic, strong) VideoViewController *videoViewController;

// We must initilise with some kind of network controller delegate, who also has delegates that should be set
- (id)initWithNetworkController:(id<NetworkControllerDelegate>)initNetworkController;

// When the network controller is ready we can begin
-(void)networkControllerIsReady;

@end
