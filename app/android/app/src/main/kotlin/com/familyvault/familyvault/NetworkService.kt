package com.familyvault.familyvault

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.app.NotificationCompat

/**
 * Foreground Service для P2P сети FamilyVault.
 * 
 * Обеспечивает:
 * - WiFi Multicast Lock для UDP discovery
 * - Wake Lock для стабильной работы
 * - Мониторинг состояния сети
 * - Работу в фоне без убийства системой
 */
class NetworkService : Service() {
    
    companion object {
        private const val TAG = "FV_NetworkService"
        private const val CHANNEL_ID = "familyvault_network"
        private const val NOTIFICATION_ID = 1001
        
        const val ACTION_START = "com.familyvault.action.START_NETWORK"
        const val ACTION_STOP = "com.familyvault.action.STOP_NETWORK"
        
        private var instance: NetworkService? = null
        
        fun isRunning(): Boolean = instance != null
        
        fun start(context: Context) {
            val intent = Intent(context, NetworkService::class.java).apply {
                action = ACTION_START
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }
        
        fun stop(context: Context) {
            val intent = Intent(context, NetworkService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }
    }
    
    private var multicastLock: WifiManager.MulticastLock? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var connectivityManager: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    
    private var isWifiConnected = false
    private var onNetworkStateChanged: ((Boolean) -> Unit)? = null
    
    override fun onCreate() {
        super.onCreate()
        instance = this
        Log.i(TAG, "NetworkService created")
        
        createNotificationChannel()
        setupConnectivityMonitor()
    }
    
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                Log.i(TAG, "Starting network service")
                startForeground(NOTIFICATION_ID, createNotification())
                acquireMulticastLock()
                acquireWakeLock()
            }
            ACTION_STOP -> {
                Log.i(TAG, "Stopping network service")
                stopSelf()
            }
        }
        return START_STICKY
    }
    
    override fun onDestroy() {
        Log.i(TAG, "NetworkService destroyed")
        
        releaseMulticastLock()
        releaseWakeLock()
        unregisterNetworkCallback()
        
        instance = null
        super.onDestroy()
    }
    
    override fun onBind(intent: Intent?): IBinder? = null
    
    // ═══════════════════════════════════════════════════════════
    // WiFi Multicast Lock
    // ═══════════════════════════════════════════════════════════
    
    private fun acquireMulticastLock() {
        if (multicastLock != null) return
        
        try {
            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicastLock = wifiManager.createMulticastLock("familyvault_discovery").apply {
                setReferenceCounted(true)
                acquire()
            }
            Log.i(TAG, "Multicast lock acquired")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to acquire multicast lock", e)
        }
    }
    
    private fun releaseMulticastLock() {
        try {
            multicastLock?.let {
                if (it.isHeld) {
                    it.release()
                    Log.i(TAG, "Multicast lock released")
                }
            }
            multicastLock = null
        } catch (e: Exception) {
            Log.e(TAG, "Failed to release multicast lock", e)
        }
    }
    
    // ═══════════════════════════════════════════════════════════
    // Wake Lock with Auto-Renewal
    // ═══════════════════════════════════════════════════════════
    
    private val wakeLockHandler = Handler(Looper.getMainLooper())
    private val wakeLockDurationMs = 9 * 60 * 1000L  // 9 минут
    private val wakeLockRenewalMs = 8 * 60 * 1000L   // Обновляем каждые 8 минут
    
    private val wakeLockRenewalRunnable = object : Runnable {
        override fun run() {
            renewWakeLock()
            wakeLockHandler.postDelayed(this, wakeLockRenewalMs)
        }
    }
    
    private fun acquireWakeLock() {
        if (wakeLock != null) return
        
        try {
            val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
            wakeLock = powerManager.newWakeLock(
                PowerManager.PARTIAL_WAKE_LOCK,
                "familyvault:network"
            ).apply {
                acquire(wakeLockDurationMs)
            }
            Log.i(TAG, "Wake lock acquired for ${wakeLockDurationMs / 1000}s")
            
            // Запускаем периодическое обновление
            wakeLockHandler.postDelayed(wakeLockRenewalRunnable, wakeLockRenewalMs)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to acquire wake lock", e)
        }
    }
    
    private fun renewWakeLock() {
        try {
            wakeLock?.let { lock ->
                if (lock.isHeld) {
                    // Сначала освобождаем, потом снова захватываем
                    lock.release()
                }
                lock.acquire(wakeLockDurationMs)
                Log.d(TAG, "Wake lock renewed for ${wakeLockDurationMs / 1000}s")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to renew wake lock", e)
        }
    }
    
    private fun releaseWakeLock() {
        try {
            // Останавливаем обновление
            wakeLockHandler.removeCallbacks(wakeLockRenewalRunnable)
            
            wakeLock?.let {
                if (it.isHeld) {
                    it.release()
                    Log.i(TAG, "Wake lock released")
                }
            }
            wakeLock = null
        } catch (e: Exception) {
            Log.e(TAG, "Failed to release wake lock", e)
        }
    }
    
    // ═══════════════════════════════════════════════════════════
    // Network Monitoring
    // ═══════════════════════════════════════════════════════════
    
    private fun setupConnectivityMonitor() {
        connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        
        val networkRequest = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()
        
        networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                Log.i(TAG, "WiFi network available")
                isWifiConnected = true
                onNetworkStateChanged?.invoke(true)
            }
            
            override fun onLost(network: Network) {
                Log.i(TAG, "WiFi network lost")
                isWifiConnected = false
                onNetworkStateChanged?.invoke(false)
            }
            
            override fun onCapabilitiesChanged(
                network: Network,
                capabilities: NetworkCapabilities
            ) {
                val hasWifi = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                if (hasWifi != isWifiConnected) {
                    isWifiConnected = hasWifi
                    onNetworkStateChanged?.invoke(hasWifi)
                }
            }
        }
        
        connectivityManager?.registerNetworkCallback(networkRequest, networkCallback!!)
        
        // Проверяем текущее состояние
        checkCurrentNetworkState()
    }
    
    private fun checkCurrentNetworkState() {
        val network = connectivityManager?.activeNetwork
        val capabilities = network?.let { connectivityManager?.getNetworkCapabilities(it) }
        isWifiConnected = capabilities?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
        Log.i(TAG, "Current WiFi state: $isWifiConnected")
    }
    
    private fun unregisterNetworkCallback() {
        try {
            networkCallback?.let {
                connectivityManager?.unregisterNetworkCallback(it)
            }
            networkCallback = null
        } catch (e: Exception) {
            Log.e(TAG, "Failed to unregister network callback", e)
        }
    }
    
    fun isWifiConnected(): Boolean = isWifiConnected
    
    fun setOnNetworkStateChanged(callback: (Boolean) -> Unit) {
        onNetworkStateChanged = callback
    }
    
    // ═══════════════════════════════════════════════════════════
    // Notification
    // ═══════════════════════════════════════════════════════════
    
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Family Network",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Синхронизация с устройствами семьи"
                setShowBadge(false)
            }
            
            val notificationManager = getSystemService(NotificationManager::class.java)
            notificationManager.createNotificationChannel(channel)
        }
    }
    
    private fun createNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("FamilyVault")
            .setContentText("Синхронизация с семьёй активна")
            .setSmallIcon(android.R.drawable.ic_menu_share)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .build()
    }
    
    fun updateNotification(connectedDevices: Int, syncStatus: String) {
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("FamilyVault")
            .setContentText(
                if (connectedDevices > 0) 
                    "Подключено: $connectedDevices устр. • $syncStatus"
                else 
                    "Поиск устройств..."
            )
            .setSmallIcon(android.R.drawable.ic_menu_share)
            .setContentIntent(
                PendingIntent.getActivity(
                    this,
                    0,
                    Intent(this, MainActivity::class.java),
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
                )
            )
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        
        val notificationManager = getSystemService(NotificationManager::class.java)
        notificationManager.notify(NOTIFICATION_ID, notification)
    }
}

