#include <lumora/platform/Clipboard.h>
#include <lumora/platform/android/AndroidApp.h>

#if __has_include(<game-activity/GameActivity.h>)
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#endif

namespace lumora::platform
{
    bool SetClipboardText(std::string_view text)
    {
#if __has_include(<game-activity/GameActivity.h>)
        JNIEnv* env = android::Env();
        android_app* app = android::App();
        if (!env || !app || !app->activity || !app->activity->javaGameActivity)
        {
            return false;
        }

        jclass activityClass = env->GetObjectClass(app->activity->javaGameActivity);
        jmethodID getSystemServiceMethod = env->GetMethodID(activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring serviceName = env->NewStringUTF("clipboard");
        jobject clipboardManager = env->CallObjectMethod(app->activity->javaGameActivity, getSystemServiceMethod, serviceName);
        env->DeleteLocalRef(serviceName);
        env->DeleteLocalRef(activityClass);

        if (!clipboardManager || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return false;
        }

        jclass clipDataClass = env->FindClass("android/content/ClipData");
        jmethodID newPlainTextMethod = env->GetStaticMethodID(clipDataClass, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
        jstring label = env->NewStringUTF("Lumora");
        std::string nullTerminated(text);
        jstring textObj = env->NewStringUTF(nullTerminated.c_str());
        jobject clipData = env->CallStaticObjectMethod(clipDataClass, newPlainTextMethod, label, textObj);
        env->DeleteLocalRef(label);
        env->DeleteLocalRef(textObj);
        env->DeleteLocalRef(clipDataClass);

        if (!clipData || env->ExceptionCheck())
        {
            env->ExceptionClear();
            env->DeleteLocalRef(clipboardManager);
            return false;
        }

        jclass clipboardManagerClass = env->GetObjectClass(clipboardManager);
        jmethodID setPrimaryClipMethod = env->GetMethodID(clipboardManagerClass, "setPrimaryClip", "(Landroid/content/ClipData;)V");
        env->CallVoidMethod(clipboardManager, setPrimaryClipMethod, clipData);
        env->DeleteLocalRef(clipData);
        env->DeleteLocalRef(clipboardManagerClass);
        env->DeleteLocalRef(clipboardManager);

        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return false;
        }
        return true;
#else
        (void)text;
        return false;
#endif
    }

    std::string GetClipboardText()
    {
#if __has_include(<game-activity/GameActivity.h>)
        JNIEnv* env = android::Env();
        android_app* app = android::App();
        if (!env || !app || !app->activity || !app->activity->javaGameActivity)
        {
            return {};
        }

        jclass activityClass = env->GetObjectClass(app->activity->javaGameActivity);
        jmethodID getSystemServiceMethod = env->GetMethodID(activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring serviceName = env->NewStringUTF("clipboard");
        jobject clipboardManager = env->CallObjectMethod(app->activity->javaGameActivity, getSystemServiceMethod, serviceName);
        env->DeleteLocalRef(serviceName);
        env->DeleteLocalRef(activityClass);

        if (!clipboardManager || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return {};
        }

        jclass clipboardManagerClass = env->GetObjectClass(clipboardManager);
        jmethodID getPrimaryClipMethod = env->GetMethodID(clipboardManagerClass, "getPrimaryClip", "()Landroid/content/ClipData;");
        jobject clipData = env->CallObjectMethod(clipboardManager, getPrimaryClipMethod);
        env->DeleteLocalRef(clipboardManagerClass);
        env->DeleteLocalRef(clipboardManager);

        if (!clipData || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return {};
        }

        jclass clipDataClass = env->GetObjectClass(clipData);
        jmethodID getItemCountMethod = env->GetMethodID(clipDataClass, "getItemCount", "()I");
        jint count = env->CallIntMethod(clipData, getItemCountMethod);
        if (count <= 0 || env->ExceptionCheck())
        {
            env->ExceptionClear();
            env->DeleteLocalRef(clipDataClass);
            env->DeleteLocalRef(clipData);
            return {};
        }

        jmethodID getItemAtMethod = env->GetMethodID(clipDataClass, "getItemAt", "(I)Landroid/content/ClipData$Item;");
        jobject item = env->CallObjectMethod(clipData, getItemAtMethod, 0);
        env->DeleteLocalRef(clipDataClass);
        env->DeleteLocalRef(clipData);

        if (!item || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return {};
        }

        jclass itemClass = env->GetObjectClass(item);
        jmethodID coerceToTextMethod = env->GetMethodID(itemClass, "coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;");
        jobject charSeq = env->CallObjectMethod(item, coerceToTextMethod, app->activity->javaGameActivity);
        env->DeleteLocalRef(itemClass);
        env->DeleteLocalRef(item);

        if (!charSeq || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return {};
        }

        jclass charSeqClass = env->GetObjectClass(charSeq);
        jmethodID toStringMethod = env->GetMethodID(charSeqClass, "toString", "()Ljava/lang/String;");
        auto str = static_cast<jstring>(env->CallObjectMethod(charSeq, toStringMethod));
        env->DeleteLocalRef(charSeqClass);
        env->DeleteLocalRef(charSeq);

        if (!str || env->ExceptionCheck())
        {
            env->ExceptionClear();
            return {};
        }

        const char* chars = env->GetStringUTFChars(str, nullptr);
        std::string result(chars ? chars : "");
        if (chars) env->ReleaseStringUTFChars(str, chars);
        env->DeleteLocalRef(str);

        return result;
#else
        return {};
#endif
    }
}
