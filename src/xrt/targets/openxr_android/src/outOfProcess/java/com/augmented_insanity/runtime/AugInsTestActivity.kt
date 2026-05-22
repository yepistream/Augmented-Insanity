// Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * Minimal launcher Activity for poking at the Aug-Ins GRS on-device without
 * needing an OpenXR client.
 *
 *   - "Start GRS"  : starts the IPC service, which triggers augins::launch
 *   - "Refresh"    : re-reads native state and redraws the panel
 *   - "Stop GRS"   : sends the SHUTDOWN_ACTION the service already supports
 *
 * Drop .augins packages into the path shown at the top of the screen, then
 * Start GRS -- modules should appear in the list with their implemented
 * function counts.
 */
package com.augmented_insanity.runtime

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import org.freedesktop.monado.ipc.BuildConfig as IpcBuildConfig
// R is generated under the openxr_android namespace declared in build.gradle.
import org.freedesktop.monado.openxr_runtime.R

class AugInsTestActivity : AppCompatActivity() {

    private lateinit var stateText: TextView
    private lateinit var pathText: TextView
    private lateinit var moduleList: TextView

    private val modulesDir by lazy { java.io.File(filesDir, "modules") }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_augins_test)

        stateText = findViewById(R.id.augins_state)
        pathText = findViewById(R.id.augins_path)
        moduleList = findViewById(R.id.augins_module_list)

        modulesDir.mkdirs()
        pathText.text = getString(R.string.augins_path_label, modulesDir.absolutePath)

        findViewById<Button>(R.id.btn_start).setOnClickListener { startService() }
        findViewById<Button>(R.id.btn_stop).setOnClickListener { stopService() }
        findViewById<Button>(R.id.btn_refresh).setOnClickListener { refresh() }

        refresh()
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun startService() {
        val intent = Intent(IpcBuildConfig.SERVICE_ACTION).setPackage(packageName)
        try {
            startForegroundService(intent)
            Log.i(TAG, "Sent SERVICE_ACTION: ${IpcBuildConfig.SERVICE_ACTION}")
        } catch (e: Exception) {
            Log.e(TAG, "startForegroundService failed", e)
        }
        // Native augins state needs a moment to populate after the service
        // thread spins up; refresh both immediately and shortly after.
        refresh()
        stateText.postDelayed({ refresh() }, 500)
    }

    private fun stopService() {
        val intent = Intent(IpcBuildConfig.SHUTDOWN_ACTION).setPackage(packageName)
        try {
            startService(intent)
            Log.i(TAG, "Sent SHUTDOWN_ACTION")
        } catch (e: Exception) {
            Log.e(TAG, "stopService failed", e)
        }
        refresh()
    }

    private fun refresh() {
        val state = AugInsBridge.getState()
        stateText.text = getString(R.string.augins_state_label, state.name)

        val modules = AugInsBridge.getModules()
        if (modules.isEmpty()) {
            moduleList.text = getString(R.string.augins_no_modules)
            return
        }
        val sb = StringBuilder()
        modules.forEachIndexed { i, mod ->
            sb.append("[${i}] ${mod.name}  (id=${mod.id}, v=${mod.version})\n")
            if (mod.implementedFunctions.isEmpty()) {
                sb.append("    (no functions hooked)\n")
            } else {
                mod.implementedFunctions.forEach { fn -> sb.append("    *  $fn\n") }
            }
        }
        moduleList.text = sb.toString()
    }

    companion object {
        private const val TAG = "AugInsTestActivity"
    }
}
