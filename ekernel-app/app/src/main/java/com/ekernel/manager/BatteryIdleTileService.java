package com.ekernel.manager;

import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.InputStreamReader;

public class BatteryIdleTileService extends TileService {
    @Override
    public void onStartListening() {
        super.onStartListening();
        updateTile();
    }

    @Override
    public void onClick() {
        super.onClick();
        Tile tile = getQsTile();
        boolean isActive = (tile.getState() == Tile.STATE_ACTIVE);
        
        try {
            Process p = Runtime.getRuntime().exec("su");
            DataOutputStream os = new DataOutputStream(p.getOutputStream());
            String value = isActive ? "0" : "1";
            os.writeBytes("mkdir -p /data/ekernel/config\n");
            os.writeBytes("printf '" + value + "\\n' > /data/ekernel/config/store_mode\n");
            os.writeBytes("printf '" + value + "\\n' > /sys/class/power_supply/battery/store_mode\n");
            os.writeBytes("exit\n");
            os.flush();
            p.waitFor();
        } catch (Exception e) {}

        updateTile();
    }

    private void updateTile() {
        Tile tile = getQsTile();
        if (tile == null) return;
        
        try {
            Process p = Runtime.getRuntime().exec("su");
            DataOutputStream os = new DataOutputStream(p.getOutputStream());
            os.writeBytes("cat /sys/class/power_supply/battery/store_mode\nexit\n");
            os.flush();
            BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String res = reader.readLine();
            p.waitFor();
            
            if ("1".equals(res)) {
                tile.setState(Tile.STATE_ACTIVE);
                tile.setLabel("Store Mode: ON");
            } else {
                tile.setState(Tile.STATE_INACTIVE);
                tile.setLabel("Store Mode: OFF");
            }
        } catch (Exception e) {
            tile.setState(Tile.STATE_UNAVAILABLE);
            tile.setLabel("Store Mode Error");
        }
        tile.updateTile();
    }
}
