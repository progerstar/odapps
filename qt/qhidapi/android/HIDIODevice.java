package ru.opendev.qhidapi;

import java.util.Set;
import java.util.HashSet;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.LinkedList;
import java.util.ArrayList;
import java.util.Queue;
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
import android.hardware.usb.UsbConstants;
import android.util.Log;

public class HIDIODevice {
    private static final String TAG = "HIDIODevice";

    private static final int REQ_TYPE_STD   = 0x00;
    private static final int REQ_TYPE_CLS   = 0x20;
    private static final int REQ_TYPE_VND   = 0x40;
    private static final int REQ_TYPE_MASK  = 0x60;

    private static final int REQ_RECP_DEV   = 0x00;
    private static final int REQ_RECP_ITF   = 0x01;
    private static final int REQ_RECP_EP    = 0x02;
    private static final int REQ_RECP_MASK  = 0x03;

    private static final int HID_REQ_GET_REPORT = 0x01;
    private static final int HID_REQ_SET_REPORT = 0x09;
    private static final int HID_REQ_GET_IDLE   = 0x02;
    private static final int HID_REQ_SET_IDLE   = 0x0A;
    private static final int HID_REQ_GET_PROTO  = 0x03;
    private static final int HID_REQ_SET_PROTO  = 0x0B;
    private static final int HID_REPORT_IN      = 0x0100;
    private static final int HID_REPORT_OUT     = 0x0200;
    private static final int HID_REPORT_FEATURE = 0x0300;

    private UsbManager mUsbManager = null;
    private UsbDevice mUsbDevice = null;
    private UsbDeviceConnection mConnection = null;

    private Set<Integer> claimed_ifaces = null;
    private String mserial;

    private Queue<byte[]>        mReceiveQueue;
    private boolean              mPermissionGranted = false;


    public HIDIODevice() {
        mReceiveQueue = new LinkedList<byte[]>();
    }

    public void setup(UsbManager mgr, UsbDevice device) {
        mUsbManager = mgr;
        mUsbDevice = device;
        mserial = mUsbDevice.getSerialNumber();
        claimed_ifaces = new HashSet<Integer>();
    }

    public boolean isOpen() {
        return (mUsbDevice != null);
    }

    public void permissionGranted(boolean valid) {
        mPermissionGranted = valid;
    }

    public boolean hasPermission() {
        return mPermissionGranted;
    }

    public String serial() {
        return (mUsbDevice!=null) ? mUsbDevice.getSerialNumber() : "";
    }

    public int configurationCount() {
        return mUsbDevice.getConfigurationCount();
    }

    public int[] deviceClass() {
        int[] tuple = new int[3];
        tuple[0] = mUsbDevice.getDeviceClass();
        tuple[1] = mUsbDevice.getDeviceSubclass();
        tuple[2] = mUsbDevice.getDeviceProtocol();
        return tuple;
    }

    public int interfaceCount() {
        return mUsbDevice.getInterfaceCount();
    }

    public int[] interfaceClass(int iface) {
        UsbInterface iObj = mUsbDevice.getInterface(iface);
        int[] tuple = new int[3];
        tuple[0] = iObj.getInterfaceClass();
        tuple[1] = iObj.getInterfaceSubclass();
        tuple[2] = iObj.getInterfaceProtocol();
        return tuple;
    }

    public int interfaceEpCount(int iface) {
        UsbInterface iObj = mUsbDevice.getInterface(iface);
        return iObj.getEndpointCount();
    }

    public boolean[] interfaceEpDirections(int iface) {
        UsbInterface iObj = mUsbDevice.getInterface(iface);
        boolean[] edirs = new boolean[iObj.getEndpointCount()];
        for(int i=0; i<iObj.getEndpointCount(); ++i) {
            edirs[i] = iObj.getEndpoint(i).getDirection() == UsbConstants.USB_DIR_OUT;
        }
        return edirs;
    }

    public int findEndpoint(int iface, int type, boolean out) {
        UsbInterface iObj = mUsbDevice.getInterface(iface);
        for(int i=0; i<iObj.getEndpointCount(); ++i) {
            UsbEndpoint ep = iObj.getEndpoint(i);
            if(
                ((out && (ep.getDirection() == UsbConstants.USB_DIR_OUT))||(!out && (ep.getDirection() == UsbConstants.USB_DIR_IN))) &&
                ((type==-1) || (ep.getType() == type))
              ) {
                return i;
            }
        }
        return -1;
    }

    public void close() {
        Log.w(TAG,"Requested device close for " + mserial);
        if(mUsbDevice != null) {
            if(mConnection != null) {
                for(Integer iface : claimed_ifaces) {
                    try {
                        mConnection.releaseInterface(mUsbDevice.getInterface(iface));
                    } catch(Exception ignored){}
                }
                mConnection.close();
                mConnection = null;
            }
            claimed_ifaces = new HashSet<Integer>();
            mUsbDevice = null;
            mPermissionGranted = false;
        }
    }

    public boolean claimInterface(int iface) {
        if(mUsbDevice != null) {
            if(mConnection == null) {
                mConnection = mUsbManager.openDevice(mUsbDevice);
            }
            if(!claimed_ifaces.contains(new Integer(iface))) {
                if(mConnection.claimInterface(mUsbDevice.getInterface(iface), true)) {
                    claimed_ifaces.add(new Integer(iface));
                    return true;
                }
                return false;
            }
            return true;
        }
        return false;
    }

    public boolean releaseInterface(int iface) {
        if((mUsbDevice != null) && (mConnection != null) && claimed_ifaces.contains(new Integer(iface))) {
            claimed_ifaces.remove(new Integer(iface));
            return mConnection.releaseInterface(mUsbDevice.getInterface(iface));
        }
        return false;
    }

    public boolean controlOut(int bmRequestType, int bRequest, int wValue, int wIndex, byte[] buffer, int length) {
        return this.controlOut(bmRequestType, bRequest, wValue, wIndex, buffer, length, 20);
    }

    public boolean controlOut(int bmRequestType, int bRequest, int wValue, int wIndex, byte[] buffer, int length, int timeout) {
        if(mConnection == null) {
            mConnection = mUsbManager.openDevice(mUsbDevice);
        }
        return mConnection.controlTransfer((bmRequestType & ~UsbConstants.USB_ENDPOINT_DIR_MASK)|UsbConstants.USB_DIR_OUT, bRequest, wValue, wIndex,
                                             buffer, length, timeout) == length;
    }

    public byte[] controlIn(int bmRequestType, int bRequest, int wValue, int wIndex, int length) {
        return this.controlIn(bmRequestType, bRequest, wValue, wIndex, length, 20);
    }

    public byte[] controlIn(int bmRequestType, int bRequest, int wValue, int wIndex, int length, int timeout) {
        if(mConnection == null) {
            mConnection = mUsbManager.openDevice(mUsbDevice);
        }
        byte[] bytes = new byte[length];
        Log.d(TAG, String.format("ControlIn: [0x%02x, 0x%02x, 0x%04x, 0x%04x]", (bmRequestType & ~UsbConstants.USB_ENDPOINT_DIR_MASK)|UsbConstants.USB_DIR_IN,
                   bRequest, wValue, wIndex));
        int ret = mConnection.controlTransfer((bmRequestType & ~UsbConstants.USB_ENDPOINT_DIR_MASK)|UsbConstants.USB_DIR_IN, bRequest, wValue, wIndex,
                                             bytes, length, timeout);
        if(ret != length) {
            Log.w(TAG, String.format("Control returned %d of %d bytes", ret, length));
            return null;
        }
        return bytes;
    }

    public boolean read() {
        return this.read(1, 0, -1, 20);
    }

    public boolean read(int ep, int timeout) {
        return this.read(ep, 0, -1, timeout);
    }

    public boolean read(int ep, int iface, int timeout) {
        return this.read(ep, iface, -1, timeout);
    }

    public boolean read(int ep, int iface, int maxsize, int timeout) {
        try {
            if(mConnection == null) {
                mConnection = mUsbManager.openDevice(mUsbDevice);
            }
            UsbInterface readIntf = mUsbDevice.getInterface(iface);
            UsbEndpoint readEp = readIntf.getEndpoint(ep);

            int packetSize = (maxsize < 0) ? readEp.getMaxPacketSize() : maxsize;
            byte[] bytes = new byte[packetSize];
            int r = mConnection.bulkTransfer(readEp, bytes, packetSize, timeout/*in MS*/);
            if (r >= 0) {
                mReceiveQueue.add(bytes); // Store received data
                Log.d(TAG, String.format("Message received of length %d", r));
            }
        } catch(Exception e) {
            Log.e(TAG, "Device read error " + e.toString());
            return false;
        }
        return true;
    }

    public boolean write(byte[] bytes, int timeout) {
        return this.write(1, 0, bytes, timeout);
    }

    public boolean write(int ep, byte[] bytes, int timeout) {
        return this.write(ep, 0, bytes, timeout);
    }

    public boolean write(int ep, int iface, byte[] bytes, int timeout) {
        try {
            if(mConnection == null) {
                mConnection = mUsbManager.openDevice(mUsbDevice);
            }
            UsbInterface writeIntf = mUsbDevice.getInterface(iface);
            UsbEndpoint writeEp = writeIntf.getEndpoint(ep);

            // Write the data as a bulk transfer with defined data length.
            int r = mConnection.bulkTransfer(writeEp, bytes, bytes.length, timeout);
            if (r != -1) {
                Log.d(TAG, String.format("Written %d bytes to %s", r, mserial));
            } else {
                Log.e(TAG, "Error happened while writing data. No ACK");
            }

            //Bulk only - send a ZLP if length == maxpacketsize
            if((writeEp.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) && (writeEp.getMaxPacketSize()>0) &&
               (bytes.length == writeEp.getMaxPacketSize())) {
                r = mConnection.bulkTransfer(writeEp, null, 0, timeout);
            }
        } catch(Exception e) {
            Log.e(TAG, "Device write error " + e.toString());
            return false;
        }
        return true;
    }

    /*
     * HID specific:
     *  GET FEATURE (control EP):
     *    Request: GET_REPORT (1)
     *    Direction: IN
     *    Request Type: Class
     *    Recepient: Interface
     *    Value: Type(Feature) | Report ID
     *    Index: interface
     */
    public byte[] hidGetFeature(int index, int rid, int length) {
        return this.controlIn(HIDIODevice.REQ_TYPE_CLS|HIDIODevice.REQ_RECP_ITF, HIDIODevice.HID_REQ_GET_REPORT, HIDIODevice.HID_REPORT_FEATURE|rid, index, length);
    }

    public byte[] hidGetFeature(int index, int rid, int length, int timeout) {
        return this.controlIn(HIDIODevice.REQ_TYPE_CLS|HIDIODevice.REQ_RECP_ITF, HIDIODevice.HID_REQ_GET_REPORT, HIDIODevice.HID_REPORT_FEATURE|rid, index, length, timeout);
    }

    public byte[] hidGetInput(int index, int rid, int length) {
        return this.controlIn(HIDIODevice.REQ_TYPE_CLS|HIDIODevice.REQ_RECP_ITF, HIDIODevice.HID_REQ_GET_REPORT, HIDIODevice.HID_REPORT_IN|rid, index, length);
    }

    public byte[] hidGetInput(int index, int rid, int length, int timeout) {
        return this.controlIn(HIDIODevice.REQ_TYPE_CLS|HIDIODevice.REQ_RECP_ITF, HIDIODevice.HID_REQ_GET_REPORT, HIDIODevice.HID_REPORT_FEATURE|rid, index, length, timeout);
    }

    public boolean hidSetFeature(int index, int rid, byte[] buffer, int length) {
        return this.controlOut(HIDIODevice.REQ_TYPE_CLS|HIDIODevice.REQ_RECP_ITF, HIDIODevice.HID_REQ_SET_REPORT, HIDIODevice.HID_REPORT_FEATURE|rid, index, buffer, length);
    }

    public boolean hidSetFeature(int index, int rid, byte[] buffer, int length, int timeout) {
        return this.controlOut(HIDIODevice.REQ_TYPE_CLS|HIDIODevice.REQ_RECP_ITF, HIDIODevice.HID_REQ_SET_REPORT, HIDIODevice.HID_REPORT_FEATURE|rid, index, buffer, length, timeout);
    }

    public boolean dataPending() {
        return !mReceiveQueue.isEmpty();
    }

    public byte[] getQueuedData() {
        return mReceiveQueue.poll();
    }
}
