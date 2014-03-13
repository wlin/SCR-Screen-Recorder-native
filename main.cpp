
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

    if (argc == 2 && strncmp(argv[1], "umount", 6) == 0) {
        return crashUnmountAudioHAL(NULL);
    } if (argc == 6) {
        testMode = true;

        reqWidth = atoi(argv[1]);
        reqHeight = atoi(argv[2]);
        initializeTransformation(argv[3]);
        allowVerticalFrames = (argv[4][0] == 'V');
        videoEncoder = atoi(argv[5]);
        sprintf(outputName, "/sdcard/scr_tests/test_%dx%d_%s_%s_%s.mp4", reqWidth, reqHeight, argv[3], argv[4], argv[5]);

        frameRate = 30;
        restrictFrameRate = false;
        rotation = 0;
        audioSource = AUDIO_SOURCE_MIC;
        videoBitrate = 10000000;
        audioSamplingRate = 16000;
    } else {

    if (argc >= 1) {
        getOutputName(argv[0]);
    } else {
        getOutputName(NULL);
    }
    if (outputName[0] != '/') {
        return processCommand();
    }

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
    getAllowVerticalFrames();

    }

    ALOGI("SETTINGS rotation: %d, audioSource: %c, resolution: %d x %d, padding: %d x %d, frameRate: %d, mode: %s, colorFix: %d, videoEncoder: %d, verticalFrames: %d",
          rotation, audioSource, reqWidth, reqHeight, paddingWidth, paddingHeight, frameRate, useGl ? "GPU" : "CPU", useBGRA, videoEncoder, allowVerticalFrames);

    printf("configured\n");
    fflush(stdout);

    if (videoEncoder < 0) {
        useOes = false;
    }

    setupInput();
    adjustRotation();

    printf("rotateView %d verticalInput %d rotation %d\n", rotateView, inputHeight > inputWidth ? 1 : 0, rotation);
    fflush(stdout);

    if (videoEncoder >= 0) {
        if (useGl) {
            output = new GLMediaRecorderOutput();
        } else {
            output = new CPUMediaRecorderOutput();
        }
    } else {
        #ifdef SCR_FFMPEG
            output = new FFmpegOutput();
        #else
            stop(199, "encoder not supported");
        #endif
    }
    output->setupOutput();

    listenForCommand();

    printf("recording\n");
    fflush(stdout);
    ALOGV("Setup finished. Starting rendering loop.");

    targetFrameTime = 1000000 / frameRate;
    int64_t startTime = getTimeMs();

    while (mrRunning && !finished) {
        if (restrictFrameRate) {
            waitForNextFrame();
        }
        frameCount++;
        output->renderFrame();
    }

    int recordingTime = getTimeMs() - startTime;
    float fps = -1.0f;
    if (recordingTime > 0) {
        fps = 1000.0f * frameCount / recordingTime;
    }
    printf("fps %f\n", fps);
    fflush(stdout);

    if (testMode) {
        if (errorCode != 0) {
            fps = 0.0f;
        }
        fprintf(stderr, "%ld, %4sx%s, %6s, %s, %2s, %4.1f\n", (long int)time(NULL), argv[1], argv[2], argv[3], argv[4], argv[5], fps);
        fflush(stderr);
    }

    if (!stopping) {
        stop(0, "finished");
    }

    interruptCommandThread();

    fixFilePermissions();
    ALOGV("main thread completed");
    return errorCode;
}

int processCommand() {
    if (strncmp(outputName, "install_audio", 13) == 0) {
        return installAudioHAL();
    } else if (strncmp(outputName, "uninstall_audio", 15) == 0) {
        return uninstallAudioHAL();
    } else if (strncmp(outputName, "mount_audio", 11) == 0) {
        return mountAudioHAL();
    } else if (strncmp(outputName, "unmount_audio", 13) == 0) {
        return unmountAudioHAL();
    } else if (strncmp(outputName, "kill_kill", 9) == 0) {
        return killKill();
    } else if (strncmp(outputName, "kill_term", 9) == 0) {
        return killTerm();
    }
    return 166;
}

void getOutputName(const char* executableName) {
    if (fgets(outputName, 512, stdin) == NULL) {
        ALOGV("cancelled");
        crashUnmountAudioHAL(executableName);
        exit(200);
        // stop(200, "cancelled");
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
        initializeTransformation(mode);
    }
}

void initializeTransformation(char *transformation) {
    if (transformation[0] == 'C') { //CPU
        useGl = false;
    } else if (transformation[0] == 'O') { //OES
        #if SCR_SDK_VERSION >= 18
        useOes = true;
        #endif
    } else if (transformation[0] == 'S') { // YUV_SP
        useGl = false;
        useYUV_SP = true;
    } else if (transformation[0] == 'P') { // YUV_P
        useGl = false;
        useYUV_P = true;
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

void getAllowVerticalFrames() {
    char allow[4];
    fgets(allow, 4, stdin);
    allowVerticalFrames = (atoi(allow) > 0);
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
    audioSource = audio[0] == '\n' ? SCR_AUDIO_MUTE : audio[0];
}

void listenForCommand() {
    if (pthread_create(&commandThread, NULL, &commandThreadStart, NULL) != 0){
        stop(216, "Can't start command thread");
    }
}

void* commandThreadStart(void* args) {
    if (testMode) {
        sleep(10);
    } else {
        char command [16];
        memset(command,'\0', 16);
        read(fileno(stdin), command, 15);
    }
    finished = true;
    commandThread = mainThread; // reset command thread id to indicate that it's stopped
    ALOGV("command thread completed");
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
    // stop(222, "killed, SIGPIPE received");
}

void sigusr1Handler(int param) {
    ALOGV("SIGUSR1 received");
    pthread_exit(0);
}

void stop(int error, const char* message) {
    stop(error, true, message);
}

void stop(int error, bool fromMainThread, const char* message) {

    //fprintf(stderr, "%d - stop requested from thread %s\n", error, getThreadName());
    //fflush(stderr);

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

    if (output != NULL) {
        output->closeOutput(fromMainThread);
    }
    closeInput();

    interruptCommandThread();

    if (fromMainThread) {
        fixFilePermissions();
        ALOGV("exiting main thread");
        exit(errorCode);
    }
}

void fixFilePermissions() {
    // on SD Card this will be ignored and in other locations this will give read access for everyone (Gallery etc.)
    if (chmod(outputName, 0664) < 0) {
        ALOGW("can't change file mode %s (%s)", outputName, strerror(errno));
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

int64_t getTimeMs() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000l + now.tv_nsec / 1000000l;
}
