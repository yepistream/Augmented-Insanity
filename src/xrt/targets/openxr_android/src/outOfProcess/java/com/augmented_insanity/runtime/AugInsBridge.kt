// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: BSL-1.0
/*!
 * Diagnostic bridge between the Aug-Ins test UI and the native augins-service
 * library. All native methods are implemented in
 * src/xrt/targets/service-lib/service_target.cpp.
 *
 * The native side reads global state inside the augins-service shared library;
 * since the Activity and the IPC Service run in the same process, both see the
 * same module list. Methods are safe to call before the GRS has launched â€”
 * they return zero/empty.
 */
package com.augmented_insanity.runtime

object AugInsBridge {
    init {
        // Same .so the IPC layer loads (see ipc/android/build.gradle
        // defaultServiceLibName). System.loadLibrary is idempotent across
        // ClassLoaders.
        System.loadLibrary("augins-service")
    }

    /**
     * Aug-Ins runtime state, mirroring augins::state_t. See augins_lifecycle.h.
     */
    enum class State(val raw: Int) {
        Launching(0),
        Ready(1),
        Connecting(2),
        Running(3),
        Disconnecting(4),
        Critical(5),
        Unknown(-1);

        companion object {
            fun fromRaw(value: Int): State = entries.firstOrNull { it.raw == value } ?: Unknown
        }
    }

    data class ModuleInfo(
        val name: String,
        val id: String,
        val version: String,
        val implementedFunctions: List<String>,
    )

    fun getState(): State = State.fromRaw(nativeGetState())

    fun getModules(): List<ModuleInfo> {
        val count = nativeGetModuleCount()
        return (0 until count).map { i ->
            val fnCount = nativeGetModuleFunctionCount(i)
            val functions = (0 until fnCount).map { j -> nativeGetModuleFunctionName(i, j) }
            ModuleInfo(
                name = nativeGetModuleName(i),
                id = nativeGetModuleId(i),
                version = nativeGetModuleVersion(i),
                implementedFunctions = functions,
            )
        }
    }

    @JvmStatic external fun nativeGetState(): Int

    @JvmStatic external fun nativeGetModuleCount(): Int

    @JvmStatic external fun nativeGetModuleName(index: Int): String

    @JvmStatic external fun nativeGetModuleId(index: Int): String

    @JvmStatic external fun nativeGetModuleVersion(index: Int): String

    @JvmStatic external fun nativeGetModuleFunctionCount(index: Int): Int

    @JvmStatic external fun nativeGetModuleFunctionName(moduleIndex: Int, functionIndex: Int): String
}
