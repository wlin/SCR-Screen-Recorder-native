
#include "main.h"

using namespace android;

int main(int argc, char* argv[]) {
    ProcessState::self()->startThreadPool();
    printf("ready\n");
    fflush(stdout);

    signal(SIGPIPE, sigpipeHandler);
    signal(SIGUSR1, sigusr1Handler);
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    mainThread = pthread_self();
    commandThread = mainThread; // will be changed when command thread is started

    getOutputName();
    getRotation();
    getAudioSetting();
    getResolution();
    getPadding();
    getFrameRate();
    getUseGl();
    getColorFormat();
    getVideoBitrate();
    getAudioSamplingRate();
    getVideoEncoder();

    ALOGI("SETTINGS rotation: %d, micAudio: %s, resolution: %d x %d, padding: %d x %d, frameRate: %d, mode: %s, colorFix: %s",
          rotation, micAudio ? "true" : "false", reqWidth, reqHeight, paddingWidth, paddingHeight, frameRate, useGl ? "GPU" : "CPU", useBGRA ? "true" : "false");

    printf("configured\n");
    fflush(stdout);

    setupInput();
    adjustRotation();

    if (videoEncoder >= 0) {
        if (useGl) {
            output = new GLMediaRecorderOutput();
        } else {
            output = new CPUMediaRecorderOutput();
        }
    } else {
        output = new FFmpegOutput();
    }
    output->setupOutput();

    listenForCommand();

    printf("recording\n");
    fflush(stdout);
    ALOGV("Setup finished. Starting rendering loop.");

    timespec frameStart;
    timespec frameEnd;
    targetFrameTime = 1000000 / frameRate;

    while (mrRunning && !finished) {
        if (restrictFrameRate) {
            waitForNextFrame();
        }
        output->renderFrame();
    }

    if (!stopping) {
        stop(0, "finished");
    }

    interruptCommandThread();

    return errorCode;
}

void getOutputName() {
    if (fgets(outputName, 512, stdin) == NULL) {
        ALOGV("cancelled");
        exit(200);
    }
    trim(outputName);
}

void getResolution() {
    char width[16];
    char height[16];
    fgets(width, 16, stdin);
    fgets(height, 16, stdin);
    reqWidth = atoi(width);
    reqHeight = atoi(height);
}

void getPadding() {
    char width[16];
    char height[16];
    fgets(width, 16, stdin);
    fgets(height, 16, stdin);
    paddingWidth = atoi(width);
    paddingHeight = atoi(height);
}

void getFrameRate() {
    char fps[16];
    fgets(fps, 16, stdin);
    frameRate = atoi(fps);
    if (frameRate == -1) {
        restrictFrameRate = false;
        frameRate = FRAME_RATE;
    } else if (frameRate <= 0 || frameRate > 100) {
        frameRate = FRAME_RATE;
    }
}

void getUseGl() {
    char mode[8];
    if (fgets(mode, 8, stdin) != NULL) {
        if (mode[0] == 'C') { //CPU
            useGl = false;
        } else if (mode[0] == 'O') { //OES
            #if SCR_SDK_VERSION >= 18
            useOes = true;
            #endif
        } else if (mode[0] == 'S') { // YUV_SP
            useGl = false;
            useYUV_SP = true;
        } else if (mode[0] == 'P') { // YUV_P
            useGl = false;
            useYUV_P = true;
        }
    }
}

void getColorFormat() {
    char mode[8];
    if (fgets(mode, 8, stdin) != NULL) {
        if (mode[0] == 'B') { //BGRA
            useBGRA = true;
        }
    }
}

void getVideoBitrate() {
    char bitrate[16];
    fgets(bitrate, 16, stdin);
    videoBitrate = atoi(bitrate);

    if (videoBitrate == 0) {
        videoBitrate = 10000000;
    }
}

void getAudioSamplingRate() {
    char sampling[16];
    fgets(sampling, 16, stdin);
    audioSamplingRate = atoi(sampling);

    if (audioSamplingRate == 0) {
        audioSamplingRate = 16000;
    }
}

void getVideoEncoder() {
    char encoder[4];
    fgets(encoder, 4, stdin);
    videoEncoder = atoi(encoder);
}

void trim(char* str) {
    while (*str) {
        if (*str == '\n') {
            *str = '\0';
        }
        str++;
    }
}

void getRotation() {
    char rot[8];
    if (fgets(rot, 8, stdin) == NULL) {
        stop(219, "No rotation specified");
    }
    rotation = atoi(rot);
}

void getAudioSetting() {
    char audio[8];
    if (fgets(audio, 8, stdin) == NULL) {
        stop(221, "No audio setting specified");
    }
    if (audio[0] == 'm') {
        micAudio = true;
    } else {
        micAudio = false;
    }
}

void listenForCommand() {
    if (pthread_create(&commandThread, NULL, &commandThreadStart, NULL) != 0){
        stop(216, "Can't start command thread");
    }
}

void* commandThreadStart(void* args) {
    char command [16];
    memset(command,'\0', 16);
    read(fileno(stdin), command, 15);
    finished = true;
    commandThread = mainThread; // reset command thread id to indicate that it's stopped
    return NULL;
}

void interruptCommandThread() {
    if (!pthread_equal(commandThread, mainThread)) {
        ALOGV("Interrupting command thread");
        pthread_kill(commandThread, SIGUSR1);
    }
}

void sigpipeHandler(int param) {
    ALOGI("SIGPIPE received");
    exit(222);
}

void sigusr1Handler(int param) {
    ALOGV("SIGUSR1 received");
    pthread_exit(0);
}

void stop(int error, const char* message) {
    stop(error, true, message);
}

void stop(int error, bool fromMainThread, const char* message) {

    fprintf(stderr, "%d - stop requested from thread %s\n", error, getThreadName());
    fflush(stderr);

    if (error == 0) {
        ALOGV("%s - stopping\n", message);
    } else {
        ALOGE("%d - stopping\n", error);
    }

    if (stopping) {
        if (errorCode == 0 && error != 0) {
            errorCode = error;
        }
        ALOGV("Already stopping");
        return;
    }

    stopping = true;
    errorCode = error;

    output->closeOutput(fromMainThread);
    closeInput();

    interruptCommandThread();

    if (fromMainThread) {
        exit(errorCode);
    }
}

const char* getThreadName() {
    pthread_t threadId = pthread_self();
    if (pthread_equal(threadId, mainThread)) {
        return "main";
    }
    if (pthread_equal(threadId, commandThread)) {
        return "command";
    }
    if (pthread_equal(threadId, stoppingThread)) {
        return "stopping";
    }

    return "other";
}

void waitForNextFrame() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long usec = now.tv_nsec / 1000;

    if (uLastFrame == -1) {
        uLastFrame = usec;
        return;
    }

    long time = usec - uLastFrame;
    if (time < 0) {
        time += 1000000;
    }

    uLastFrame = usec;

    if (time < targetFrameTime) {
        usleep(targetFrameTime - time);
    }
}
