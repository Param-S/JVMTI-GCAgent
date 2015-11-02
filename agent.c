#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include <jvmti.h>

// socket includes
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

/* Global static data */
static jvmtiEnv *jvmti;
static long int gc_count;
static jrawMonitorID lock;
int thread_started = 0;
int client_connected = 0;
int listenfd = 0;
int connfd = 0;


// socket initialization
int socket_init(void) 
{
    struct sockaddr_in serv_addr;
    int numrv;  

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;    
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serv_addr.sin_port = htons(5250);    

    bind(listenfd, (struct sockaddr*)&serv_addr,sizeof(serv_addr));
    if(listen(listenfd, 10) == -1){
        printf("Failed to listen\n");
        return -1;
    }

    return 0;
}

static void JNICALL
socket_listener(jvmtiEnv* jvmti, JNIEnv* jni, void *p) 
{
    /* loop until successfully connected to a client */
    fprintf(stderr, "socket listener waiting for clients\n");
    while ((connfd = accept(listenfd, (struct sockaddr*)NULL ,NULL)) == -1) ;
    client_connected = 1;
    fprintf(stderr, "Client connected.. Hurray!!!\n");
}

// gc_state 1 => gc start, 0 => gc end
void send_message(gc_state) 
{
    char sendBuff[1025];
    memset(sendBuff, '0', sizeof(sendBuff));

    if(gc_state)
	sprintf(sendBuff, "GCStart:%ld", gc_count);
    else {
	sprintf(sendBuff, "GCEnd:%ld", gc_count);
    }
    write(connfd, sendBuff, strlen(sendBuff));
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

    fprintf(stderr, "VMInit...\n");
    ret = socket_init();
    if(ret != 0) {
        fprintf(stderr, "WARNING:  Socket Failed to initialize, err=%d", ret);
    } else {
    	err = (*jvmti)->RunAgentThread(jvmti, alloc_thread(env), &socket_listener, NULL, JVMTI_THREAD_NORM_PRIORITY);
    	if (err != JVMTI_ERROR_NONE) {
            fprintf(stderr, "ERROR: Socket Listener failed, err=%d\n", err);
    	} else {
            thread_started = 1;
	}
    }
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_START */
static void JNICALL 
gc_start(jvmtiEnv* jvmti) 
{
    jvmtiError err;
    fprintf(stderr, "GarbageCollectionStart...\n");
    if(thread_started && client_connected) {
    	err = (*jvmti)->RawMonitorEnter(jvmti, lock);
        gc_count++;
        err = (*jvmti)->RawMonitorExit(jvmti, lock);
        send_message(1);
    }
    
}

/* Callback for JVMTI_EVENT_GARBAGE_COLLECTION_FINISH */
static void JNICALL 
gc_finish(jvmtiEnv* jvmti_env) 
{
    jvmtiError err;
    fprintf(stderr, "GarbageCollectionFinish...\n");
    if(thread_started && client_connected) {
        send_message(0);
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

    /* Create the necessary raw monitor */
    err = (*jvmti)->CreateRawMonitor(jvmti, "lock", &lock);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "ERROR: Unable to create raw monitor: %d\n", err);
        return -1;
    }
    return 0;

}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
    if(thread_started && client_connected) {
    	close(connfd);
    }
}
