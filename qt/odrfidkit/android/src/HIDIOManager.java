package ru.opendev;

import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.LinkedList;
import java.util.ArrayList;
import java.util.Queue;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.util.Log;

public class HIDIOManager {
    private static final String TAG = "HIDIOManager";
    private static final String ACTION_USB_PERMISSION = "ru.opendev.USB_PERMISSION";

    private Context mcontext;
    private int mproductId;
    private int mvendorId;

    // Locker object that is responsible for locking read/write thread.
    private Object mlocker = new Object();
    private Thread mreadingThread = null;
    private String mdeviceName;
    private UsbManager           mUsbManager = null;
    private UsbDevice            mUsbDevice = null;
    private String               mserial;
    private Queue<byte[]>        mReceiveQueue;

    /**
     * Creates a hid bridge to the dongle. Should be created once.
     * @param context is the UI context of Android.
     * @param productId of the device.
     * @param vendorId of the device.
     */
    public HIDIOManager(Context context, int productId, int vendorId) {
        mcontext = context;
        mproductId = productId;
        mvendorId = vendorId;
        mReceiveQueue = new LinkedList<byte[]>();
    }

    public String[] deviceList() {
        if (mUsbManager == null) {
            mUsbManager = (UsbManager) mcontext.getSystemService(Context.USB_SERVICE);
        }
        List<String> snList = new ArrayList<>();

        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();

        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        mUsbDevice = null;

        // Iterate all the available devices and find ours.
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
            if (device.getProductId() == mproductId && device.getVendorId() == mvendorId) {
                snList.add(device.getSerialNumber());
            }
        }

        String[] snArray = snList.toArray(new String[] {});
        return snArray;
    }

    /**
     * Searches for the device and opens it if successful
     * @return true, if connection was successful
     */
    public boolean open(String sn) {
        mUsbManager = (UsbManager) mcontext.getSystemService(Context.USB_SERVICE);
        mserial = sn;
        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();

        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        mUsbDevice = null;

        // Iterate all the available devices and find ours.
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
            if ( (device.getProductId() == mproductId) && (device.getVendorId() == mvendorId) &&
                 ((sn.length() == 0) || (sn.equals(device.getSerialNumber())))){
                mUsbDevice = device;
                mdeviceName = mUsbDevice.getDeviceName();
                mserial = mUsbDevice.getSerialNumber();

                UsbInterface intf = mUsbDevice.getInterface(0);
                UsbEndpoint ep;
                for (int i = 0; i < intf.getEndpointCount(); i++)
                {
                    ep = intf.getEndpoint(i);
                    Log.w(TAG, String.format("Endpoint %d: %s, type %d, dir %d", i, ep.toString(), ep.getType(), ep.getDirection()));
                }
                break;
            }
        }

        if (mUsbDevice == null) {
            Log.w(TAG, "Cannot find the device. Is it connected?");
            Log.w(TAG, String.format("\t searched for VendorId: %s and ProductId: %s", mvendorId, mproductId));
            return false;
        }

        // Create and intent and request a permission.
        PendingIntent mPermissionIntent = PendingIntent.getBroadcast(mcontext, 0, new Intent(ACTION_USB_PERMISSION), 0);
        IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
        mcontext.registerReceiver(mUsbReceiver, filter);

        mUsbManager.requestPermission(mUsbDevice, mPermissionIntent);
        Log.d(TAG, "Found the device");
        return true;
    }

    public boolean isOpen() {
        return (mUsbDevice != null);
    }

    public String serial() {
        return (mUsbDevice!=null) ? mUsbDevice.getSerialNumber() : "";
    }

    /**
     * Closes the reading thread of the device.
     */
    public void close() {
        if(mUsbDevice != null) {
        stopReadingThread();
        mUsbDevice = null;
        }
    }

    /**
     * Starts the thread that continuously reads the data from the device.
     * Should be called in order to be able to talk with the device.
     */
    public void startReadingThread() {
        if (mreadingThread == null) {
            mreadingThread = new Thread(readerReceiver);
            mreadingThread.start();
        } else {
            Log.d(TAG, "Reading thread already started");
        }
    }

    /**
     * Stops the thread that continuously reads the data from the device.
     * If it is stopped - talking to the device would be impossible.
     */
    @SuppressWarnings("deprecation")
    public void stopReadingThread() {
        if (mreadingThread != null) {
            // Just kill the thread. It is better to do that fast if we need that asap.
            mreadingThread.stop();
            mreadingThread = null;
        } else {
            Log.d(TAG, "No reading thread to stop");
        }
    }

    /**
     * Write data to the usb hid. Data is written as-is, so calling method is responsible for adding header data.
     * @param bytes is the data to be written.
     * @return true if succeed.
     */
    public boolean write(byte[] bytes) {
        try
        {
            // Lock that is common for read/write methods.
            synchronized (mlocker) {
                UsbInterface writeIntf = mUsbDevice.getInterface(0);
                UsbEndpoint writeEp = writeIntf.getEndpoint(1/*OUT*/);
                UsbDeviceConnection writeConnection = mUsbManager.openDevice(mUsbDevice);

                // Lock the usb interface.
                writeConnection.claimInterface(writeIntf, true);

                // Write the data as a bulk transfer with defined data length.
                int r = writeConnection.bulkTransfer(writeEp, bytes, bytes.length, 0);
                if (r != -1) {
                    Log.d(TAG, String.format("Written %s bytes to the dongle. Data written: %s", r, composeString(bytes)));
                } else {
                    Log.e(TAG, "Error happened while writing data. No ACK");
                }

                // Release the usb interface.
                writeConnection.releaseInterface(writeIntf);
                writeConnection.close();
            }

        } catch(NullPointerException e)
        {
            Log.e(TAG, "Error happend while writing. Could not connect to the device or interface is busy?");
            Log.e(TAG, Log.getStackTraceString(e));
            return false;
        }
        return true;
    }

    /**
     * @return true if there are any data in the queue to be read.
     */
    public boolean dataPending() {
        synchronized(mlocker) {
            return !mReceiveQueue.isEmpty();
        }
    }

    /**
     * Queue the data from the read queue.
     * @return queued data.
     */
    public byte[] getDataFromQueue() {
        synchronized(mlocker) {
            return mReceiveQueue.poll();
        }
    }

    // The thread that continuously receives data from the dongle and put it to the queue.
    private Runnable readerReceiver = new Runnable() {
        public void run() {
            if (mUsbDevice == null) {
                Log.w(TAG, "No device to read from");
                return;
            }

            UsbEndpoint readEp;
            UsbDeviceConnection readConnection = null;
            UsbInterface readIntf = null;
            boolean readerStartedMsgWasShown = false;

            // We will continuously ask for the data from the device and store it in the queue.
            while (true) {
                // Lock that is common for read/write methods.
                synchronized (mlocker) {
                    try
                    {
                        if (mUsbDevice == null) {
                            open(mserial);
                            Log.w(TAG, "No device. Recheking in 10 sec...");

                            sleep(10000);
                            continue;
                        }

                        readIntf = mUsbDevice.getInterface(0);
                        readEp = readIntf.getEndpoint(0/*IN*/);
                        if (!mUsbManager.getDeviceList().containsKey(mdeviceName)) {
                            Log.w(TAG, "Failed to connect to the device. Retrying to acquire it.");
                            open(mserial);
                            if (!mUsbManager.getDeviceList().containsKey(mdeviceName)) {
                                Log.w(TAG, "No device. Rechecking in 10 sec...");

                                sleep(10000);
                                continue;
                            }
                        }

                        try
                        {

                            readConnection = mUsbManager.openDevice(mUsbDevice);

                            if (readConnection == null) {
                                Log.w(TAG, "Cannot start reader because the user didn't gave me permissions or the device is not present. Retrying in 2 sec...");
                                sleep(2000);
                                continue;
                            }

                            // Claim and lock the interface in the android system.
                            readConnection.claimInterface(readIntf, true);
                        }
                        catch (SecurityException e) {
                            Log.w(TAG, "Cannot start reader because the user didn't gave me permissions. Retrying in 2 sec...");

                            sleep(2000);
                            continue;
                        }

                        // Show the reader started message once.
                        if (!readerStartedMsgWasShown) {
                            Log.d(TAG, "!!! Reader was started !!!");
                            readerStartedMsgWasShown = true;
                        }

                        // Read the data as a bulk transfer with the size = MaxPacketSize
                        int packetSize = readEp.getMaxPacketSize();
                        byte[] bytes = new byte[packetSize];
                        int r = readConnection.bulkTransfer(readEp, bytes, packetSize, 50);
                        if (r >= 0) {
                            byte[] trancatedBytes = new byte[r]; // Truncate bytes in the honor of r

                            int i=0;
                            for (byte b : bytes) {
                                trancatedBytes[i] = b;
                                i++;
                            }

                            mReceiveQueue.add(trancatedBytes); // Store received data
                            Log.d(TAG, String.format("Message received of length %s and content: %s", r, composeString(bytes)));
                        }

                        // Release the interface lock.
                        readConnection.releaseInterface(readIntf);
                        readConnection.close();
                        }

                    catch (NullPointerException e) {
                        Log.e(TAG, "Error happened while reading. No device or the connection is busy");
                        Log.e(TAG, Log.getStackTraceString(e));
                    }
                    catch (ThreadDeath e) {
                        if (readConnection != null) {
                            readConnection.releaseInterface(readIntf);
                            readConnection.close();
                        }

                        throw e;
                    }
                }

                // Sleep for 10 ms to pause, so other thread can write data or anything.
                // As both read and write data methods lock each other - they cannot be run in parallel.
                // Looks like Android is not so smart in planning the threads, so we need to give it a small time
                // to switch the thread context.
                sleep(10);
            }
        }
    };

    private void sleep(int milliseconds) {
        try {
            Thread.sleep(milliseconds);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {

        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                synchronized (this) {
                    UsbDevice device = (UsbDevice)intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);

                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        if(device != null){
                          //call method to set up device communication
                          HIDIOManager.this.startReadingThread();
                       }
                    }
                    else {
                        Log.d(TAG, "permission denied for the device " + device);
                    }
                }
            }
        }
    };

    /**
     * Composes a string from byte array.
     */
    private String composeString(byte[] bytes) {
        StringBuilder builder = new StringBuilder();
        for (byte b: bytes) {
            builder.append(b);
            builder.append(" ");
        }

        return builder.toString();
    }
}
