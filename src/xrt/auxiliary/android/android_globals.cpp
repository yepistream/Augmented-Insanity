// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for Android-specific global state.
 * @author Rylie Pavlik <rylie.pavlik@collabora.com>
 * @ingroup aux_android
 */

#include "android_globals.h"

#include <stddef.h>
#include <string>
#include <wrap/android.app.h>

/*!
 * @todo Do we need locking here?
 */
static struct
{
	struct _JavaVM *vm = nullptr;
	jni::Object activity = {};
	jni::Object context = {};
	struct _ANativeWindow *window = nullptr;
	std::string augins_modules_dir;
	std::string augins_cache_dir;
} android_globals;

void
android_globals_store_vm_and_activity(struct _JavaVM *vm, void *activity)
{
	jni::init(vm);
	android_globals.vm = vm;
	android_globals.activity = jni::Object((jobject)activity);
}

void
android_globals_store_vm_and_context(struct _JavaVM *vm, void *context)
{
	jni::init(vm);
	android_globals.vm = vm;
	android_globals.context = jni::Object((jobject)context);
	if (android_globals_is_instance_of_activity(vm, context)) {
		android_globals.activity = jni::Object((jobject)context);
	}
}

bool
android_globals_is_instance_of_activity(struct _JavaVM *vm, void *obj)
{
	jni::init(vm);

	auto activity_cls = jni::Class(wrap::android::app::Activity::getTypeName());
	return JNI_TRUE == jni::env()->IsInstanceOf((jobject)obj, activity_cls.getHandle());
}

void
android_globals_store_window(struct _ANativeWindow *window)
{
	android_globals.window = window;
}

struct _ANativeWindow *
android_globals_get_window()
{
	return android_globals.window;
}

struct _JavaVM *
android_globals_get_vm()
{
	return android_globals.vm;
}

void *
android_globals_get_activity()
{
	return android_globals.activity.getHandle();
}

void *
android_globals_get_context()
{
	return android_globals.context.isNull() ? android_globals.activity.getHandle()
	                                        : android_globals.context.getHandle();
}

void
android_globals_store_augins_paths(const char *modules_dir, const char *cache_dir)
{
	if (modules_dir != nullptr) {
		android_globals.augins_modules_dir = modules_dir;
	}
	if (cache_dir != nullptr) {
		android_globals.augins_cache_dir = cache_dir;
	}
}

const char *
android_globals_get_augins_modules_dir(void)
{
	return android_globals.augins_modules_dir.empty() ? nullptr
	                                                  : android_globals.augins_modules_dir.c_str();
}

const char *
android_globals_get_augins_cache_dir(void)
{
	return android_globals.augins_cache_dir.empty() ? nullptr
	                                                : android_globals.augins_cache_dir.c_str();
}
