// The "slider" view on Android: a SeekBar from 0 to 100.
package org.eskiu.examples;

import android.content.Context;
import android.view.View;
import android.widget.SeekBar;
import org.eskiu.ui.UiActivity;

public final class Slider {
    public static void setup() {
        UiActivity.registerView("slider", new UiActivity.ViewFactory() {
            @Override public View create(Context context, UiActivity.ViewEvents events) {
                SeekBar bar = new SeekBar(context);
                bar.setMax(100);
                bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    @Override public void onProgressChanged(SeekBar b, int value, boolean user) {
                        if (user) events.emit("change", Integer.toString(value));
                    }
                    @Override public void onStartTrackingTouch(SeekBar b) {}
                    @Override public void onStopTrackingTouch(SeekBar b) {}
                });
                return bar;
            }
            @Override public void set(View view, String name, String value) {
                if (name.equals("value") && !value.isEmpty()) ((SeekBar)view).setProgress(Integer.parseInt(value));
            }
        });
    }
}
