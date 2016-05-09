#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include <jvmti.h>

#include <errno.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>


/* Global static data */
static jvmtiEnv *jvmti;
static long int gc_count = 0;
static jrawMonitorID lock;
int thread_started = 0;
int ptime = 4;
char cmd[1024]={'l','s','\0'};

pthread_mutex_t agnt_lock;
pthread_cond_t agnt_cvar;

static void JNICALL
gc_listener(jvmtiEnv* jvmti, JNIEnv* jni, void *p)
{
    struct timespec wait_time;
    int pid;
    pthread_mutex_lock(&agnt_lock);
    do {
        if (gc_count == 1) {
	    clock_gettime(CLOCK_REALTIME, &wait_time);
	    wait_time.tv_sec += ptime;
	    if (pthread_cond_timedwait(&agnt_cvar, &agnt_lock, &wait_time) == ETIMEDOUT && gc_count == 1) {
		/* take defined action */	
		fprintf(stderr, "gc pause time exceeded defined value\n");
		pid = fork();
		if (pid == 0) {
		    execvp(cmd, NULL);
		}
	    } 
	} 
        pthread_cond_wait(&agnt_cvar, &agnt_lock);
    } while (1);
}

/* Creates a new jthread */
static jthread 
alloc_thread(JNIEnv *env) {

    jclass    thrClass;
    jmethodID cid;
    jthread   res;

    thrClass = (*env)->FindClass(env, "java/lang/Thread");
    cid      = (*env)->GetMethodID(env, thrClass, "<init>", "()V");
    res      = (*env)->NewObject(env, thrClass, cid);
    return res;

}

/* Callback for JVMTI_EVENT_VM_INIT */
static void JNICALL 
vm_init(jvmtiEnv *jvmti, JNIEnv *env, jthread thread) {
    
    jvmtiError err;
    int ret;

    err = (*jvmti)->RunAgentThread(jvmti, alloc_thread(env), &gc_listener, NULL, JVMTI_THREAD_NORM_PRIORITY);
    if (err != JVMTI_ERROR_NONE) {
       fprintf(stderr, "ERROR: failed to create gc agent thread, err=%d\n", err);
    } else {
	thread_started = (pthread_mutex_init(&agnt_lock, NULL) == 0)?(pthread_cond_init(&agnt_cvar, NULL) == 0)?1:0:0;
    }
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_START */
static void JNICALL 
gc_start(jvmtiEnv* jvmti) 
{
    jvmtiError err;
    fprintf(stderr, "GarbageCollectionStart...\n");
    if(thread_started) {
	pthread_mutex_lock(&agnt_lock);
        gc_count = 1;
	pthread_cond_signal(&agnt_cvar);
	pthread_mutex_unlock(&agnt_lock);
    }
    
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_FINISH */
static void JNICALL 
gc_finish(jvmtiEnv* jvmti_env) 
{
    jvmtiError err;
    fprintf(stderr, "GarbageCollectionFinish...\n");
    if(thread_started) {
	pthread_mutex_lock(&agnt_lock);
	gc_count = 0;
	pthread_cond_signal(&agnt_cvar);
	pthread_mutex_unlock(&agnt_lock);
    }
}

/* Agent_OnLoad() is called first, we prepare for a VM_INIT event here. */
JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {

    jint                rc;
    jvmtiError          err;
    jvmtiCapabilities   capabilities;
    jvmtiEventCallbacks callbacks;

    /* Get JVMTI environment */
    rc = (*vm)->GetEnv(vm, (void **)&jvmti, JVMTI_VERSION_1);
    if (rc != JNI_OK) {
        fprintf(stderr, "ERROR: Unable to create jvmtiEnv, GetEnv failed, error=%d\n", rc);
        return -1;
    }

    /* Get/Add JVMTI capabilities */ 
    err = (*jvmti)->GetCapabilities(jvmti, &capabilities);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: GetCapabilities failed, error=%d\n", err);
        return -1;
    }

    capabilities.can_generate_garbage_collection_events = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &capabilities);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: AddCapabilities failed, error=%d\n", err);
        return -1;
    }

    /* Set callbacks and enable event notifications */
    memset(&callbacks, 0, sizeof(callbacks));

    callbacks.VMInit                  = &vm_init;
    callbacks.GarbageCollectionStart  = &gc_start;
    callbacks.GarbageCollectionFinish = &gc_finish;
    err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: SetEventCallbacks failed, error=%d\n", err);
        return -1;
    }
 
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: SetEventNotificationMode for VM_INIT failed, error=%d\n", err);
        return -1;
    }
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_GARBAGE_COLLECTION_START, NULL);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: SetEventNotificationMode for GC_START failed, error=%d\n", err);
        return -1;
    }
    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, NULL);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: SetEventNotificationMode GC_FINISH failed, error=%d\n", err);
        return -1;
    }

    /* parse the agent options & set corresponding variables */
    sscanf(options, "pausetime=%d,command=%s", &ptime, cmd);    
    return 0;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
    if(thread_started) {
	/* cleanup required */
    }
}
