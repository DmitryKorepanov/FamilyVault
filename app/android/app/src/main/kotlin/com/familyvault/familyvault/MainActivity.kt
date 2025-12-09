package com.familyvault.familyvault

import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.annotation.NonNull
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

/**
 * MainActivity с поддержкой P2P Network Service через MethodChannel.
 */
class MainActivity : FlutterActivity() {
    
    companion object {
        private const val CHANNEL = "com.familyvault/network_service"
    }
    
    override fun configureFlutterEngine(@NonNull flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "startNetworkService" -> {
                    startNetworkService()
                    result.success(true)
                }
                "stopNetworkService" -> {
                    stopNetworkService()
                    result.success(true)
                }
                "isNetworkServiceRunning" -> {
                    result.success(NetworkService.isRunning())
                }
                "isWifiConnected" -> {
                    result.success(isWifiConnected())
                }
                "requestBatteryOptimizationExemption" -> {
                    requestBatteryOptimizationExemption()
                    result.success(true)
                }
                "isBatteryOptimizationExempt" -> {
                    result.success(isBatteryOptimizationExempt())
                }
                else -> {
                    result.notImplemented()
                }
            }
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Автозапуск сервиса при открытии приложения (опционально)
        // startNetworkService()
    }
    
    private fun startNetworkService() {
        NetworkService.start(this)
    }
    
    private fun stopNetworkService() {
        NetworkService.stop(this)
    }
    
    private fun isWifiConnected(): Boolean {
        val connectivityManager = getSystemService(CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        val network = connectivityManager.activeNetwork
        val capabilities = network?.let { connectivityManager.getNetworkCapabilities(it) }
        return capabilities?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true
    }
    
    // ═══════════════════════════════════════════════════════════
    // Battery Optimization
    // ═══════════════════════════════════════════════════════════
    
    private fun requestBatteryOptimizationExemption() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val powerManager = getSystemService(POWER_SERVICE) as android.os.PowerManager
            val packageName = packageName
            
            if (!powerManager.isIgnoringBatteryOptimizations(packageName)) {
                val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                    data = android.net.Uri.parse("package:$packageName")
                }
                startActivity(intent)
            }
        }
    }
    
    private fun isBatteryOptimizationExempt(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val powerManager = getSystemService(POWER_SERVICE) as android.os.PowerManager
            return powerManager.isIgnoringBatteryOptimizations(packageName)
        }
        return true // До Android M нет ограничений
    }
}
