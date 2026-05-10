// Copyright 2020, Collabora, Ltd.
// Copyright 2024-2025, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  Library exposing IPC server.
 * @author Rylie Pavlik <rylie.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#include "jnipp.h"
#include "jni.h"

#include "wrap/android.view.h"

#include "server/ipc_server.h"
#include "server/ipc_server_interface.h"
#include "server/ipc_server_mainloop_android.h"
#include "util/u_logging.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "android/android_globals.h"

#include "xrt/xrt_config_build.h"

#ifdef XRT_FEATURE_AUG_INS
#include "augins_dispatch.h"
#include "augins_lifecycle.h"
#endif

#include <chrono>
#include <memory>
#include <thread>

using wrap::android::view::Surface;
using namespace std::chrono_literals;

namespace {
struct IpcServerHelper
{
public:
	static IpcServerHelper &
	instance()
	{
		static IpcServerHelper instance;
		return instance;
	}

	void
	signalStartupComplete(ipc_server *s)
	{
		std::unique_lock<std::mutex> lock{server_mutex};
		startup_complete = true;
		server = s;
		startup_cond.notify_all();
	}

	void
	startServer()
	{
		std::unique_lock lock(server_mutex);
		if (!server && !server_thread) {
#ifdef XRT_FEATURE_AUG_INS
			// Launch the Aug-Ins GRS *before* the IPC server thread so the
			// dispatch table and module lifecycle hooks are live by the time
			// any client can connect. See design doc Ã‚Â§8 (Launching_STATE).
			const char *modules_dir = android_globals_get_augins_modules_dir();
			const char *cache_dir = android_globals_get_augins_cache_dir();
			if (modules_dir && cache_dir) {
				augins::launch(modules_dir, cache_dir);
			} else {
				U_LOG_W("Aug-Ins paths not set by Java side; skipping module load");
			}
#endif
			server_thread =
			    std::make_unique<std::thread>([&]() { ipc_server_main_common(&ismi, &callbacks, this); });
		}
	}

	static void
	signalInitFailed(xrt_result_t xret, void *data)
	{
		static_cast<IpcServerHelper *>(data)->signalStartupComplete(nullptr);
	}

	static void
	signalStartupCompleteTrampoline(ipc_server *s, xrt_instance *xsint, void *data)
	{
		static_cast<IpcServerHelper *>(data)->signalStartupComplete(s);
	}

	static void
	signalShuttingDownTrampoline(ipc_server *s, xrt_instance *xsint, void *data)
	{
		// No-op
	}

	static void
	signalClientConnectedTrampoline(struct ipc_server *s, uint32_t client_id, void *data)
	{
#ifdef XRT_FEATURE_AUG_INS
		augins::on_client_connecting();
#endif
	}

	static void
	signalClientDisconnectedTrampoline(struct ipc_server *s, uint32_t client_id, void *data)
	{
#ifdef XRT_FEATURE_AUG_INS
		augins::on_client_disconnecting();
#endif
	}

	int32_t
	addClient(int fd)
	{
		if (!waitForStartupComplete()) {
			return -1;
		}
		return ipc_server_mainloop_add_fd(server, &server->ml, fd);
	}

	int32_t
	shutdownServer()
	{
		if (!server || !server_thread) {
			// Should not happen.
			U_LOG_E("service: shutdownServer called before server started up!");
			return -1;
		}

		{
			// Wait until IPC server stop
			std::unique_lock lock(server_mutex);
			ipc_server_handle_shutdown_signal(server);
			server_thread->join();
			server_thread.reset(nullptr);
			server = NULL;
			startup_complete = false;
		}

		return 0;
	}

private:
	IpcServerHelper() {}

	bool
	waitForStartupComplete()
	{
		std::unique_lock<std::mutex> lock{server_mutex};
		bool completed = startup_cond.wait_for(lock, START_TIMEOUT_SECONDS, [&]() { return startup_complete; });

		if (!server) {
			U_LOG_E("Failed to create ipc server");
		}

		if (!completed) {
			U_LOG_E("Server startup timeout!");
		}
		return server && completed;
	}

	const struct ipc_server_main_info ismi = {
	    .udgci =
	        {
	            .window_title = "Monado Android Service",
	            .open = U_DEBUG_GUI_OPEN_NEVER,
	        },
	    .exit_on_disconnect = false,
	    .no_stdin = false,
	};

	const struct ipc_server_callbacks callbacks = {
	    .init_failed = signalInitFailed,
	    .mainloop_entering = signalStartupCompleteTrampoline,
	    .mainloop_leaving = signalShuttingDownTrampoline,
	    .client_connected = signalClientConnectedTrampoline,
	    .client_disconnected = signalClientDisconnectedTrampoline,
	};

	//! Reference to the ipc_server, managed by ipc_server_process
	struct ipc_server *server = NULL;

	//! Mutex for starting thread
	std::mutex server_mutex;

	//! Server thread
	std::unique_ptr<std::thread> server_thread{};

	//! Condition variable for starting thread
	std::condition_variable startup_cond;

	//! Server startup state
	bool startup_complete = false;

	//! Timeout duration in seconds
	static constexpr std::chrono::seconds START_TIMEOUT_SECONDS = 40s;
};
} // namespace

extern "C" JNIEXPORT void JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeSetAugInsPaths(JNIEnv *env,
                                                                jobject /*thiz*/,
                                                                jstring modulesDir,
                                                                jstring cacheDir)
{
	const char *modules_c = env->GetStringUTFChars(modulesDir, nullptr);
	const char *cache_c = env->GetStringUTFChars(cacheDir, nullptr);
	android_globals_store_augins_paths(modules_c, cache_c);
	env->ReleaseStringUTFChars(modulesDir, modules_c);
	env->ReleaseStringUTFChars(cacheDir, cache_c);
}

extern "C" JNIEXPORT void JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeStartServer(JNIEnv *env, jobject thiz, jobject context)
{
	JavaVM *jvm = nullptr;
	jint result = env->GetJavaVM(&jvm);
	assert(result == JNI_OK);
	assert(jvm);

	jni::init(env);
	jni::Object monadoImpl(thiz);
	U_LOG_D("service: Called nativeStartServer");

	android_globals_store_vm_and_context(jvm, context);

	IpcServerHelper::instance().startServer();
}

extern "C" JNIEXPORT jint JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeAddClient(JNIEnv *env, jobject thiz, int fd)
{
	jni::init(env);
	jni::Object monadoImpl(thiz);
	U_LOG_D("service: Called nativeAddClient with fd %d", fd);

	int native_fd = dup(fd);
	U_LOG_D("service: transfer ownership to native and native_fd %d", native_fd);

	// We try pushing the fd number to the server. If and only if we get a 0 return, has the server taken ownership.
	return IpcServerHelper::instance().addClient(native_fd);
}

extern "C" JNIEXPORT void JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeAppSurface(JNIEnv *env, jobject thiz, jobject surface)
{
	jni::init(env);
	Surface surf(surface);
	jni::Object monadoImpl(thiz);

	ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
	android_globals_store_window((struct _ANativeWindow *)nativeWindow);
	U_LOG_D("Stored ANativeWindow: %p", (void *)nativeWindow);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeShutdownServer(JNIEnv *env, jobject thiz)
{
	jni::init(env);
	jni::Object monadoImpl(thiz);

	jint ret = IpcServerHelper::instance().shutdownServer();
#ifdef XRT_FEATURE_AUG_INS
	augins::shutdown();
#endif
	return ret;
}

#ifdef XRT_FEATURE_AUG_INS

// ---------------------------------------------------------------------------
// Aug-Ins diagnostic JNI bridge.
//
// Backs com.augmented_insanity.runtime.AugInsBridge in the openxr_android
// module. The Activity calls these to render the runtime state in a debug UI
// without going through the OpenXR/IPC path. All methods are safe to call
// before augins::launch Ã¢â‚¬â€ they return zero/empty until the GRS comes up.
// ---------------------------------------------------------------------------

static jstring
to_jstring(JNIEnv *env, const char *s)
{
	return env->NewStringUTF(s ? s : "");
}

extern "C" JNIEXPORT jint JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetState(JNIEnv * /*env*/, jclass /*cls*/)
{
	return augins_status_state();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleCount(JNIEnv * /*env*/,
                                                                       jclass /*cls*/)
{
	return static_cast<jint>(augins_status_module_count());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleName(JNIEnv *env,
                                                                      jclass /*cls*/,
                                                                      jint index)
{
	return to_jstring(env, augins_status_module_name(static_cast<size_t>(index)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleId(JNIEnv *env,
                                                                    jclass /*cls*/,
                                                                    jint index)
{
	return to_jstring(env, augins_status_module_id(static_cast<size_t>(index)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleVersion(JNIEnv *env,
                                                                         jclass /*cls*/,
                                                                         jint index)
{
	return to_jstring(env, augins_status_module_version(static_cast<size_t>(index)));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleFunctionCount(JNIEnv * /*env*/,
                                                                               jclass /*cls*/,
                                                                               jint index)
{
	return static_cast<jint>(augins_status_module_function_count(static_cast<size_t>(index)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleFunctionName(JNIEnv *env,
                                                                              jclass /*cls*/,
                                                                              jint module_index,
                                                                              jint function_index)
{
	return to_jstring(env, augins_status_module_function_name(static_cast<size_t>(module_index),
	                                                          static_cast<size_t>(function_index)));
}

#endif // XRT_FEATURE_AUG_INS
