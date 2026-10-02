import QtQuick 2.15

Page2Form {

    function decimalToHex(d, padding) {
        var hex = Number(d).toString(16);
        padding = typeof (padding) === "undefined" || padding === null ? padding = 2 : padding;

        while (hex.length < padding) {
            hex = "0" + hex;
        }

        return hex;
    }

    function decimalToChar(d) {
        if(d>=16)
        {
            return "0";
        }

        return Number(d).toString(16).toUpperCase();
    }

    writebutton.onClicked: {

        if(settings.read("legacy","false")==="true") {
            settings.write("t1","%1".arg(page2.t1Box.currentIndex),"int");
            if(page2.t1Box.currentIndex>9) {
                serialCommand("~W9\n");
            } else {
                serialCommand("~W%1\n".arg(settings.read("t1","5")))
            }
            return;
        }

        var str = "~W";
        str += decimalToChar(page2.t1Box.currentIndex);
        str += decimalToChar(page2.t2Box.currentIndex);
        if(page2.t3Group.enabled === true)
        {
            //Pro2
            str += decimalToChar(page2.t3Box.currentIndex);
            str += decimalToChar(page2.t4Box.currentIndex);
            str += decimalToChar(page2.t5Box.currentIndex);
            str += decimalToChar(page2.p6Box.currentIndex);
            str += decimalToChar(page2.p7Box.currentIndex);

            str += decimalToChar(page2.p8Box.currentIndex);
            str += decimalToChar(page2.p9Box.currentIndex);
            str += decimalToHex(page2.p10spinBox.value);
        }
        serialCommand(str);
    }

    readbutton.onClicked: {
        serialCommand("~F");
    }

}
