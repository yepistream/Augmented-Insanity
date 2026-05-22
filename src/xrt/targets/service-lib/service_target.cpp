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
#include "host_api.h"
#include "loader.h"
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
			// Aug-Ins v0.2 service-side module loader. Phase 2a:
			// the call below is a stub that logs and returns. Phase
			// 2b fills in the real scan + dlopen. Either way, this
			// has to fire BEFORE ipc_server_main_common spawns its
			// dispatch threads so the dispatch registry is fully
			// populated before any client can hit it.
			//
			// Paths come from the runtime APK Java side via the
			// android_globals_*_augins_*_dir helpers (set during
			// MonadoImpl's native init). Either may be NULL on a
			// non-Android dev run; the loader tolerates that.
			augins_loader_init(
			    android_globals_get_augins_modules_dir(),
			    android_globals_get_augins_cache_dir());
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
		// v0.1 augins::on_client_connecting() removed in Phase 1 demolition.
	}

	static void
	signalClientDisconnectedTrampoline(struct ipc_server *s, uint32_t client_id, void *data)
	{
		// v0.1 augins::on_client_disconnecting() removed in Phase 1 demolition.
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

#ifdef XRT_FEATURE_AUG_INS
	// Hand JVM/Context to the Aug-Ins host API so modules can call
	// host->get_jvm() / host->get_context() from aug_on_module_load.
	// Done before startServer() so the loader sees them populated.
	augins_host_api_set_jvm_ctx(static_cast<void *>(jvm),
	                            static_cast<void *>(context));
#endif

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
	// Tear down loaded modules (lifecycle unload + dlclose) AFTER the
	// IPC server has stopped accepting new client traffic and the
	// dispatch threads have joined, so no module function is mid-call
	// when its .so gets unmapped.
	augins_loader_shutdown();
#endif
	return ret;
}

// ---------------------------------------------------------------------------
// Aug-Ins diagnostic JNI bridge.
//
// Phase 1 demolition (2026-05-19): the v0.1 augins_status_*() backing
// functions have been deleted. These JNI implementations are stubbed
// to return zero/empty so AugInsBridge.kt + AugInsTestActivity.kt
// still load and link without UnsatisfiedLinkError. The Activity will
// just display "no modules". v0.2 service-side will reintroduce real
// status accessors at this position.
// (Original header comment block follows; left intact for context.)
//
// Backs com.augmented_insanity.runtime.AugInsBridge in the openxr_android
// module. The Activity calls these to render the runtime state in a debug UI
// without going through the OpenXR/IPC path. All methods are safe to call
// before augins::launch -- they return zero/empty until the GRS comes up.
// ---------------------------------------------------------------------------

static jstring
to_jstring(JNIEnv *env, const char *s)
{
	return env->NewStringUTF(s ? s : "");
}

extern "C" JNIEXPORT jint JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetState(JNIEnv * /*env*/, jclass /*cls*/)
{
	return -1; // Unknown state (was augins::state_t)
}

extern "C" JNIEXPORT jint JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleCount(JNIEnv * /*env*/, jclass /*cls*/)
{
	return 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleName(JNIEnv *env, jclass /*cls*/, jint /*index*/)
{
	return to_jstring(env, "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleId(JNIEnv *env, jclass /*cls*/, jint /*index*/)
{
	return to_jstring(env, "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleVersion(JNIEnv *env, jclass /*cls*/, jint /*index*/)
{
	return to_jstring(env, "");
}

extern "C" JNIEXPORT jint JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleFunctionCount(JNIEnv * /*env*/, jclass /*cls*/, jint /*index*/)
{
	return 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_augmented_1insanity_runtime_AugInsBridge_nativeGetModuleFunctionName(JNIEnv *env, jclass /*cls*/, jint /*module_index*/, jint /*function_index*/)
{
	return to_jstring(env, "");
}
