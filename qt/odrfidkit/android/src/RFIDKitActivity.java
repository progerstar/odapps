package ru.opendev;

import android.content.Context;
import android.view.WindowManager;
import android.view.Gravity;
import android.util.Log;


public class RFIDKitActivity extends org.qtproject.qt5.android.bindings.QtActivity {

    private static final String TAG = "ODRFIDKit";

    private static final int USB_DEFAULT_VID = 0x0483;
    private static final int USB_DEFAULT_PID = 0xA26A;

    private HIDIOManager mio = null;

    @Override
    public void onCreate(android.os.Bundle savedInstanceState){
        super.onCreate(savedInstanceState);

        mio = new HIDIOManager(this, RFIDKitActivity.USB_DEFAULT_PID, RFIDKitActivity.USB_DEFAULT_VID);
    }

    @Override
    public void onStart(){
        super.onStart();
    }

    @Override
    public void onDestroy(){
        mio.close();
        super.onDestroy();
    }

    /*Translator*/
    public String[] listHID() {
        return mio.deviceList();
    }

    public boolean openHID(String sn) {
        return mio.open(sn);
    }

    public boolean isOpenHID() {
        return mio.isOpen();
    }

    public String serialHID() {
        return mio.serial();
    }

    public void closeHID() {
        mio.close();
    }

    public boolean canReadHID() {
        return mio.dataPending();
    }

    public byte[] readHID() {
        return mio.getDataFromQueue();
    }

    public boolean writeHID(byte[] bytes) {
         return mio.write(bytes);
    }
}
