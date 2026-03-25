package com.ekernel.manager

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.topjohnwu.superuser.Shell

class ElbActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 48, 48, 48)
            setBackgroundColor(Color.parseColor("#121212"))
        }

        val title = TextView(this).apply {
            text = "eLoadBalancer (ELB)"
            textSize = 28f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 48)
        }
        layout.addView(title)

        val enableSwitch = Switch(this).apply {
            text = "Enable Custom ELB"
            setTextColor(Color.LTGRAY)
            textSize = 18f
            setPadding(0, 16, 0, 32)
        }
        layout.addView(enableSwitch)

        val reserveSwitch = Switch(this).apply {
            text = "Reserve Core 7 for Heavy Tasks"
            setTextColor(Color.LTGRAY)
            textSize = 18f
            setPadding(0, 16, 0, 32)
        }
        layout.addView(reserveSwitch)

        val packingSwitch = Switch(this).apply {
            text = "Enable LITTLE Packing"
            setTextColor(Color.LTGRAY)
            textSize = 18f
            setPadding(0, 16, 0, 48)
        }
        layout.addView(packingSwitch)

        val applyBtn = Button(this).apply {
            text = "Apply Settings"
            setBackgroundColor(Color.parseColor("#3498db"))
            setTextColor(Color.WHITE)
            setOnClickListener {
                applyElbSettings(enableSwitch.isChecked, reserveSwitch.isChecked, packingSwitch.isChecked)
            }
        }
        layout.addView(applyBtn)

        setContentView(layout)

        loadCurrentState(enableSwitch, reserveSwitch, packingSwitch)
    }

    private fun loadCurrentState(enable: Switch, reserve: Switch, packing: Switch) {
        Shell.cmd("cat /sys/kernel/ekernel/elb/enabled").submit {
            runOnUiThread { enable.isChecked = (it.out.joinToString("").trim() == "1") }
        }
        Shell.cmd("cat /sys/kernel/ekernel/elb/reserve_core").submit {
            runOnUiThread { reserve.isChecked = (it.out.joinToString("").trim() == "1") }
        }
        Shell.cmd("cat /sys/kernel/ekernel/elb/packing_enable").submit {
            runOnUiThread { packing.isChecked = (it.out.joinToString("").trim() == "1") }
        }
    }

    private fun applyElbSettings(enabled: Boolean, reserve: Boolean, packing: Boolean) {
        val cmds = listOf(
            "echo ${if (enabled) 1 else 0} > /sys/kernel/ekernel/elb/enabled",
            "echo ${if (reserve) 1 else 0} > /sys/kernel/ekernel/elb/reserve_core",
            "echo ${if (packing) 1 else 0} > /sys/kernel/ekernel/elb/packing_enable"
        )
        Shell.cmd(*cmds.toTypedArray()).submit {
            val success = it.isSuccess
            runOnUiThread {
                if (success) {
                    Toast.makeText(this, "ELB Settings Applied!", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Failed. Is ELB compiled in the kernel?", Toast.LENGTH_LONG).show()
                }
            }
        }
    }
}
