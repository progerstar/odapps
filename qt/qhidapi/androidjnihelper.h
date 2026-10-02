#ifndef ANDROIDJNIHELPER_H
#define ANDROIDJNIHELPER_H

#include <QtAndroid>
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QDebug>
#include <QVector>

#define VA_ARGS_CALL(...) , ##__VA_ARGS__

#define ANDROID_JNI_SAFE_EXECUTE(env, caller, method, signature, ...)\
    do{\
        (caller).callMethod<void>((method), (signature) VA_ARGS_CALL(__VA_ARGS__));\
        if((env)->ExceptionCheck()) {\
            qWarning()<<"JNI exception while executing "<<(method);\
            (env)->ExceptionClear();\
        }\
    }while(0)

#define ANDROID_JNI_SAFE_GET(J_TYPE, C_TYPE, default_value, output, env, caller, method, signature, ...)\
    do{\
        J_TYPE __temp_return_value = (caller).callMethod<J_TYPE>((method), (signature) VA_ARGS_CALL(__VA_ARGS__));\
        if((env)->ExceptionCheck()) {\
            qWarning()<<"JNI exception while calling "<<(method);\
            (env)->ExceptionClear();\
            (output) = (default_value);\
        }\
        (output) = C_TYPE(__temp_return_value);\
    }while(0)

#define ANDROID_JNI_SAFE_GET_ARRAY(J_TYPE, ENV_GET_TYPE, C_TYPE, env, output, success, caller, method, signature, ...) \
    do{\
        (output).clear();\
        (success) = false;\
        QAndroidJniObject __temp_array_object = (caller).callObjectMethod((method), (signature) VA_ARGS_CALL(__VA_ARGS__));\
        if((env)->ExceptionCheck()) {\
            qWarning()<<"JNI exception while executing "<<(method);\
            (env)->ExceptionClear();\
        } else if(__temp_array_object.isValid()) {\
            J_TYPE ## Array __temp_array_j = __temp_array_object.object<J_TYPE ## Array>();\
            int __temp_array_size = (env)->GetArrayLength(__temp_array_j);\
            J_TYPE* __temp_array_data = (env)->Get ## ENV_GET_TYPE ## ArrayElements(__temp_array_j, nullptr);\
            if(__temp_array_data) {\
                (output).reserve(__temp_array_size);\
                for(int __temp_counter=0;__temp_counter<__temp_array_size;++__temp_counter){\
                    (output).append(C_TYPE(__temp_array_data[__temp_counter]));\
                }\
                (env)->Release ## ENV_GET_TYPE ## ArrayElements(__temp_array_j, __temp_array_data, JNI_ABORT);\
                (success) = true;\
            }\
        } else {\
            qWarning()<<"Method "<<(method)<<" returned invalid object";\
        }\
    }while(0)

#define ANDROID_JNI_SAFE_GET_BYTES(env, output, success, caller, method, signature, ...)\
    do{\
        (output).clear();\
        (success) = false;\
        QAndroidJniObject __temp_array_object = (caller).callObjectMethod((method), (signature) VA_ARGS_CALL(__VA_ARGS__));\
        if((env)->ExceptionCheck()) {\
            qWarning()<<"JNI exception while executing "<<(method);\
            (env)->ExceptionClear();\
        } else if(__temp_array_object.isValid()) {\
            jbyteArray __temp_array_j = __temp_array_object.object<jbyteArray>();\
            int __temp_array_size = (env)->GetArrayLength(__temp_array_j);\
            jbyte* __temp_array_data = (env)->GetByteArrayElements(__temp_array_j, nullptr);\
            if(__temp_array_data) {\
                (output).clear(); (output).resize(__temp_array_size);\
                memcpy((output).data(), __temp_array_data, __temp_array_size);\
                (env)->ReleaseByteArrayElements(__temp_array_j, __temp_array_data, JNI_ABORT);\
                (success) = true;\
            }\
        } else {\
            qWarning()<<"Method "<<(method)<<" returned invalid object";\
        }\
    }while(0)

#define ANDROID_JNI_SAFE_GET_STRINGLIST(env, output, success, caller, method, signature, ...)\
    do{\
        (success) = false;\
        QAndroidJniObject __temp_array_object = (caller).callObjectMethod((method), (signature) VA_ARGS_CALL(__VA_ARGS__));\
        if((env)->ExceptionCheck()) {\
            qWarning()<<"JNI exception while executing "<<(method);\
            (env)->ExceptionClear();\
        } else if(__temp_array_object.isValid()) {\
            jobjectArray __temp_array_j = __temp_array_object.object<jobjectArray>();\
            int __temp_array_size = (env)->GetArrayLength(__temp_array_j);\
            (output).clear(); (output).reserve(__temp_array_size);\
            (success) = true;\
            for(int __temp_counter=0;__temp_counter<__temp_array_size;++__temp_counter){\
                jstring __temp_string = (jstring) ((env)->GetObjectArrayElement(__temp_array_j, __temp_counter));\
                if(__temp_string) {\
                    const char* __temp_utf = (env)->GetStringUTFChars(__temp_string, nullptr);\
                    (output).append(QString(__temp_utf));\
                    (env)->ReleaseStringUTFChars(__temp_string, __temp_utf);\
                    (env)->DeleteLocalRef(__temp_string);\
                } else {\
                    qWarning()<<"NULL string in java array";\
                    (output).append(QString());\
                }\
            }\
        } else {\
            qWarning()<<"Method "<<(method)<<" returned invalid object";\
        }\
    }while(0)

#define ANDROID_CLEAR_EXCEPTION(env, message)\
    do{\
        if((env)->ExceptionCheck()) {\
            qWarning()<<(message);\
            (env)->ExceptionClear();\
        }\
    }while(0)

namespace AndroidJNIHelper {
    bool requestPermission(const QString& id);
}

#endif // ANDROIDJNIHELPER_H
