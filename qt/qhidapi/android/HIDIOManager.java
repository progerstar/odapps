package ru.opendev.qhidapi;

import java.lang.Integer;
import java.util.Map;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map.Entry;
import java.util.List;
import java.util.LinkedList;
import java.util.ArrayList;
import java.util.Queue;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.concurrent.atomic.AtomicBoolean;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Process;
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

    private UsbManager mUsbManager = null;
    private Map<String,HIDIODevice> devPool = null;

    /**
     * Creates a hid bridge to the dongle. Should be created once.
     * @param context is the UI context of Android.
     * @param productId of the device.
     * @param vendorId of the device.
     */
    public HIDIOManager(Context context) {
        mcontext = context;
        devPool = new HashMap<String,HIDIODevice>();
    }

    public static String deviceID(final UsbDevice device) {
        return String.format("%04x:%04x:", device.getVendorId(), device.getProductId()) + device.getSerialNumber();
    }

    public static String deviceInfo(final UsbDevice device) {
        StringBuilder sb = new StringBuilder();
        sb.append(String.format("%04x/%04x", device.getVendorId(), device.getProductId()));
        sb.append("/");
        sb.append(device.getManufacturerName());
        sb.append("/");
        sb.append(device.getProductName());
        sb.append("/");
        sb.append(device.getSerialNumber());
        return sb.toString();
    }

    public String[] deviceList(int vendorId) {
        return this.deviceList(vendorId, -1);
    }

    public String[] deviceList(int vendorId, int productId) {
        return this.deviceList(vendorId, productId, "");
    }

    public String[] deviceList(int vendorId, int productId, final String product) {
        if (mUsbManager == null) {
            mUsbManager = (UsbManager) mcontext.getSystemService(Context.USB_SERVICE);
        }
        List<String> snList = new ArrayList<>();
        Pattern prodp = Pattern.compile((product.length() > 0) ? product : ".*");

        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();

        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();

        // Iterate all the available devices and find ours.
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
            Log.d(TAG, String.format("Enumerating USB device %04x:%04x:", device.getVendorId(), device.getProductId()));
            if (((productId < 0) || (device.getProductId() == productId)) &&
                (device.getVendorId() == vendorId) &&
                prodp.matcher(device.getProductName()).matches()) {
                snList.add(this.deviceID(device));
            }
        }

        String[] snArray = snList.toArray(new String[] {});
        return snArray;
    }

    public String[] deviceList(final String vendor, final String product) {
        if (mUsbManager == null) {
            mUsbManager = (UsbManager) mcontext.getSystemService(Context.USB_SERVICE);
        }
        List<String> snList = new ArrayList<>();
        Log.d(TAG, "Starting USB enumerate for " + vendor + "/" + product);

        Pattern vndp = Pattern.compile((vendor.length() > 0) ? vendor : ".*");
        Pattern prodp = Pattern.compile((product.length() > 0) ? product : ".*");

        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();

        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();

        // Iterate all the available devices and find ours.
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
            Log.d(TAG, "Enumerating USB device " + device.getManufacturerName() + "/" + device.getProductName());
            if (vndp.matcher(device.getManufacturerName()).matches() &&
                prodp.matcher(device.getProductName()).matches()) {
                snList.add(this.deviceID(device));
            }
        }

        String[] snArray = snList.toArray(new String[] {});
        return snArray;
    }

    public String[] deviceInfoList(int vendorId, int productId, final String product) {
        if (mUsbManager == null) {
            mUsbManager = (UsbManager) mcontext.getSystemService(Context.USB_SERVICE);
        }
        List<String> snList = new ArrayList<>();
        Pattern prodp = Pattern.compile((product.length() > 0) ? product : ".*");

        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();

        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();

        // Iterate all the available devices and find ours.
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
            Log.d(TAG, String.format("Enumerating USB device %04x:%04x:", device.getVendorId(), device.getProductId()));
            if (((productId < 0) || (device.getProductId() == productId)) &&
                (device.getVendorId() == vendorId) &&
                prodp.matcher(device.getProductName()).matches()) {
                snList.add(this.deviceInfo(device));
            }
        }

        String[] snArray = snList.toArray(new String[] {});
        return snArray;
    }

    /**
     * Searches for the device and opens it if successful
     * @return true, if connection was successful
     */
    public boolean open(int vendorId, int productId, final String sn) {
        return this.open(String.format("%04x:%04x:", vendorId, productId) + sn);
    }

    public boolean open(final String descr) {
        /* VVVV:PPPP:serial */
        Pattern p = Pattern.compile("([0-9A-Fa-f]{4,4})\\:([0-9A-Fa-f]{4,4})\\:(.+)");
        Matcher m = p.matcher(descr);
        if(!m.matches()) {
            Log.w(TAG, descr + " does not match the expected pattern");
            return false;
        }

        if(devPool.get(descr) != null) {
            devPool.get(descr).close();
            devPool.remove(descr);
        }

        mUsbManager = (UsbManager) mcontext.getSystemService(Context.USB_SERVICE);
        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();

        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        int mvendorId = Integer.parseInt(m.group(1), 16);
        int mproductId = Integer.parseInt(m.group(2), 16);
        final String sn = m.group(3);

        // Iterate all the available devices and find ours.
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
            if ( (device.getProductId() == mproductId) && (device.getVendorId() == mvendorId) &&
                 ((sn.length() == 0) || (sn.equals(device.getSerialNumber())))) {

                HIDIODevice dev = new HIDIODevice();
                dev.setup(mUsbManager, device);
                devPool.put(descr, dev);

                PendingIntent mPermissionIntent = PendingIntent.getBroadcast(mcontext, 0, new Intent(ACTION_USB_PERMISSION), 0);
                IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
                mcontext.registerReceiver(mUsbReceiver, filter);

                mUsbManager.requestPermission(device, mPermissionIntent);
                Log.d(TAG, "Found the device for " + sn);
                return true;
            }
        }

        Log.w(TAG, "Cannot find the device. Is it connected?");
        Log.w(TAG, String.format("\t searched for VendorId: %d and ProductId: %d", mvendorId, mproductId));
        return false;
    }

    public boolean isOpen(final String sn) {
        return devPool.containsKey(sn) && devPool.get(sn).isOpen();
    }

    public boolean hasPermission(final String sn) {
        return devPool.containsKey(sn) && devPool.get(sn).hasPermission();
    }


    /**
     * Closes the reading thread of the device.
     */
    public void close(final String sn) {
        Log.w(TAG,"Requested device close for"+sn);

        if(devPool.containsKey(sn)) {
            devPool.get(sn).close();
            devPool.remove(sn);
        }
    }

    public void closeAll() {
        Iterator<Entry<String,HIDIODevice>> it = devPool.entrySet().iterator();
        while(it.hasNext()) {
            Entry<String,HIDIODevice> dev = it.next();
            try{
                dev.getValue().close();
                it.remove();
            } catch(Exception e) {
                Log.w(TAG, "Error closing device");
            }
        }
    }

    public void startReadingThread() {
        //NOP
    }

    public void stopReadingThread() {
        //NOP
    }

    public HIDIODevice handle(final String sn) {
        try{
            HIDIODevice hnd = devPool.get(sn);
            if(hnd != null) {
                Log.d(TAG, "JNI requested device handle" + hnd.toString());
                return hnd;
            }
        } catch(SecurityException e) {
            Log.w(TAG, "The user did not give permission to use "+sn);
        } catch(NullPointerException e) {
            Log.w(TAG, "Device " + sn + " is not opened");
        }
        return null;
    }

    private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {

        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                synchronized (this) {
                    UsbDevice device = (UsbDevice)intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);

                    if(device != null){
                        final String did = HIDIOManager.deviceID(device);
                        if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                            Log.d(TAG, "Permission granted for the device " + did);
                            if(devPool.containsKey(did)) {
                                Log.d(TAG, "Device is in pool: " + devPool.get(did));
                                devPool.get(did).permissionGranted(true);
                                Log.d(TAG, "Device " + devPool.get(did) + " - access granted");
                            }
                        } else {
                            Log.d(TAG, "permission denied for the device " + device);
                            devPool.remove(did);
                        }
                    }
                }
            }
            Log.d(TAG, "bcast receiver exit");
        }
    };
}
