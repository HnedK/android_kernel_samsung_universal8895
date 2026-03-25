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
            if (isActive) {
                os.writeBytes("printf '0\\n' > /sys/class/power_supply/battery/store_mode\n");
            } else {
                os.writeBytes("printf '1\\n' > /sys/class/power_supply/battery/store_mode\n");
            }
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
                tile.setLabel("Bypass: ON");
            } else {
                tile.setState(Tile.STATE_INACTIVE);
                tile.setLabel("Bypass: OFF");
            }
        } catch (Exception e) {
            tile.setState(Tile.STATE_UNAVAILABLE);
            tile.setLabel("Bypass Error");
        }
        tile.updateTile();
    }
}
