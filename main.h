#ifndef SCREENREC_MAIN_H
#define SCREENREC_MAIN_H

#include "screenrec.h"
#include "mediarecorder_output.h"
#ifdef SCR_FFMPEG
#include "ffmpeg_output.h"
#endif
#include "audio_hal_installer.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <cutils/log.h>
#include <cutils/sched_policy.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/prctl.h>
#include <stdlib.h>

#include <binder/ProcessState.h>

// Configuration parameters
char outputName [512];
int rotation;
char audioSource = SCR_AUDIO_MUTE;
int reqWidth = 0;
int reqHeight = 0;
int paddingWidth = 0;
int paddingHeight = 0;
int frameRate = 0;
bool restrictFrameRate = true;
bool useGl = true;
bool useOes = false;
int videoBitrate;
int audioSamplingRate;
bool useBGRA = false;
bool useYUV_P = false;
bool useYUV_SP = false;
int videoEncoder = 0;
bool allowVerticalFrames = true;

// Output
int outputFd;
int videoWidth, videoHeight;

// global state
bool finished = false;
bool stopping = false;
int errorCode = 0;
int64_t startTime = 0ll;
bool mrRunning = false;
int frameCount = 0;

// private
ScrOutput *output;
bool testMode = false;

// pthreads
pthread_t mainThread;
pthread_t stoppingThread;
pthread_t commandThread;

// frame timers
long uLastFrame = -1;

void getRotation();
void getAudioSetting();
void getOutputName(const char* executableName);
void getResolution();
void getPadding();
void getFrameRate();
void getUseGl();
void getColorFormat();
void getVideoBitrate();
void getAudioSamplingRate();
void getVideoEncoder();
void getAllowVerticalFrames();
void trim(char* str);
void initializeTransformation(char* transform);
void closeOutput();
void closeInput();
void adjustRotation();
void waitForNextFrame();
void* commandThreadStart(void* args);
void listenForCommand();
void interruptCommandThread();
void sigpipeHandler(int param);
void sigusr1Handler(int param);
void fixFilePermissions();
const char* getThreadName();
int processCommand();

#endif
