package com.eight086tiny;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

public class MainActivity extends Activity {
    static {
        try {
            java.io.FileWriter fw = new java.io.FileWriter("/sdcard/8086tiny/startup.log", false);
            fw.write("STATIC BLOCK START\n");
            fw.close();
        } catch (Exception e) {}
        Log.i("8086tiny", ">>> STATIC BLOCK START <<<");
        Log.i("8086tiny", "Loading sdl-1.2 library");
        System.loadLibrary("sdl-1.2");
        Log.i("8086tiny", "Loading sdl_main library");
        System.loadLibrary("sdl_main");
        Log.i("8086tiny", "Loading 8086tiny library");
        System.loadLibrary("8086tiny");
        Log.i("8086tiny", "Libraries loaded successfully");
        Log.i("8086tiny", ">>> STATIC BLOCK END <<<");
        try {
            java.io.FileWriter fw = new java.io.FileWriter("/sdcard/8086tiny/startup.log", true);
            fw.write("STATIC BLOCK END\n");
            fw.close();
        } catch (Exception e) {}
    }

    public native void nativeInit8086(String curdir, String cmdline);
    public native void nativeStepFrame8086();
    public native void nativeSetEmulatorView(android.view.View view);
    public native void nativeKey(int keyCode, int down, int unicode, int gamepadId);
    public native void nativeInjectKey(int keyCode, int down, int unicode);
    public native void nativeReset8086();
    public native int[] nativeGetFramebuffer8086();
    public native void nativeGetScreenSize8086(int[] size);
    public native void nativeTestFont8086();
    
    private EmulatorView emulatorView;
    private volatile Thread emulatorThread = null;
    private volatile boolean running = false;
    private int frameWidth = 320;
    private int frameHeight = 200;
    
@Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            java.io.FileWriter fw = new java.io.FileWriter("/sdcard/8086tiny/startup.log", true);
            fw.write("onCreate START\n");
            fw.close();
        } catch (Exception e) {}
        Log.i("8086tiny", "=== onCreate START ===");
        
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        
        emulatorView = new EmulatorView(this);
        setContentView(emulatorView);
        Log.i("8086tiny", "=== onCreate: after setContentView ===");
        
        int[] size = new int[2];
        nativeGetScreenSize8086(size);
        frameWidth = size[0];
        frameHeight = size[1];
        Log.i("8086tiny", "=== onCreate: Screen size: " + frameWidth + "x" + frameHeight);
        
        nativeSetEmulatorView(emulatorView);
        Log.i("8086tiny", "=== onCreate: after nativeSetEmulatorView ===");

        running = true;
        Log.i("8086tiny", "=== onCreate: starting emulator thread ===");
        emulatorThread = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    java.io.FileWriter fw = new java.io.FileWriter("/sdcard/8086tiny/startup.log", true);
                    fw.write("Emulator thread STARTED\n");
                    fw.close();
                } catch (Exception e) {}
                Log.i("8086tiny", "=== Emulator thread STARTED (id=" + Thread.currentThread().getId() + ") ===");
                try {
                    try {
                        java.io.FileWriter fw = new java.io.FileWriter("/sdcard/8086tiny/startup.log", true);
                        fw.write("About to call nativeInit8086\n");
                        fw.close();
                    } catch (Exception e) {}
                    Log.i("8086tiny", "=== About to call nativeInit8086 ===");
                    nativeInit8086("/sdcard/8086tiny", "bios fd.img");
                    try {
                        java.io.FileWriter fw = new java.io.FileWriter("/sdcard/8086tiny/startup.log", true);
                        fw.write("nativeInit8086 returned\n");
                        fw.close();
                    } catch (Exception e) {}
                    Log.i("8086tiny", "=== nativeInit8086 returned ===");
                } catch (UnsatisfiedLinkError e) {
                    Log.e("8086tiny", "=== UnsatisfiedLinkError in nativeInit8086: " + e.getMessage() + " ===");
                    e.printStackTrace();
                    return;
                } catch (Exception e) {
                    Log.e("8086tiny", "=== Exception in nativeInit8086: " + e.getMessage() + " ===");
                    e.printStackTrace();
                    return;
                } catch (Error e) {
                    Log.e("8086tiny", "=== Error in nativeInit8086: " + e.getMessage() + " ===");
                    e.printStackTrace();
                    return;
                }
                int frameCount = 0;
                while (running) {
                    nativeStepFrame8086();
                    // Render the frame to the SurfaceView
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            if (emulatorView != null) {
                                emulatorView.renderFrame();
                            }
                        }
                    });
                    frameCount++;
                    if (frameCount <= 3 || frameCount % 500 == 0) {
                        Log.i("8086tiny", "Emulator loop: frameCount=" + frameCount);
                    }
                    try {
                        Thread.sleep(16);
                    } catch (InterruptedException e) {
                        break;
                    }
                }
                Log.i("8086tiny", "Emulator thread FINISHED");
            }
        }, "8086tiny-Emulator");
        Log.i("8086tiny", "MainActivity: emulator thread created, starting...");
        emulatorThread.start();
        Log.i("8086tiny", "MainActivity: emulator thread started");
    }
    
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        // Allow MENU key to open options menu
        if (keyCode == KeyEvent.KEYCODE_MENU) {
            return super.onKeyDown(keyCode, event);
        }
        Log.i("8086tiny", "onKeyDown: keyCode=" + keyCode + ", unicode=" + event.getUnicodeChar());
        try {
            nativeInjectKey(keyCode, 1, event.getUnicodeChar());
            Log.i("8086tiny", "nativeInjectKey called successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e("8086tiny", "nativeInjectKey UnsatisfiedLinkError: " + e.getMessage());
        } catch (Exception e) {
            Log.e("8086tiny", "nativeInjectKey exception: " + e.getMessage());
        }
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        // Allow MENU key to open options menu
        if (keyCode == KeyEvent.KEYCODE_MENU) {
            return super.onKeyUp(keyCode, event);
        }
        Log.i("8086tiny", "onKeyUp: keyCode=" + keyCode + ", unicode=" + event.getUnicodeChar());
        try {
            nativeInjectKey(keyCode, 0, event.getUnicodeChar());
            Log.i("8086tiny", "nativeInjectKey called successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e("8086tiny", "nativeInjectKey UnsatisfiedLinkError: " + e.getMessage());
        } catch (Exception e) {
            Log.e("8086tiny", "nativeInjectKey exception: " + e.getMessage());
        }
        return true;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        // Intercept all key events (including physical keyboard) before system handling
        int action = event.getAction();
        int keyCode = event.getKeyCode();
        int unicode = event.getUnicodeChar();
        
        // Allow MENU key to open options menu
        if (keyCode == KeyEvent.KEYCODE_MENU) {
            return super.dispatchKeyEvent(event);
        }
        
        if (action == KeyEvent.ACTION_DOWN) {
            Log.i("8086tiny", "dispatchKeyEvent DOWN: keyCode=" + keyCode + ", unicode=" + unicode);
            nativeInjectKey(keyCode, 1, unicode);
            return true;
        } else if (action == KeyEvent.ACTION_UP) {
            Log.i("8086tiny", "dispatchKeyEvent UP: keyCode=" + keyCode + ", unicode=" + unicode);
            nativeInjectKey(keyCode, 0, unicode);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    
    @Override
    protected void onDestroy() {
        Log.i("8086tiny", "=== onDestroy: stopping emulator thread ===");
        running = false;
        Thread t = emulatorThread;
        if (t != null) {
            t.interrupt();
            try {
                t.join(2000);
                Log.i("8086tiny", "=== onDestroy: emulator thread joined ===");
            } catch (InterruptedException e) {
                Log.i("8086tiny", "=== onDestroy: join interrupted ===");
            }
        }
        Log.i("8086tiny", "=== onDestroy: calling System.exit(0) ===");
        System.exit(0);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (emulatorView != null) {
            emulatorView.requestFocus();
        }
    }

    @Override
    public boolean onCreateOptionsMenu(android.view.Menu menu) {
        menu.add(0, 1, 0, "Reset Emulation");
        menu.add(0, 2, 0, "Font Test");
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(android.view.MenuItem item) {
        if (item.getItemId() == 1) {
            Log.i("8086tiny", "Reset Emulation requested");
            nativeReset8086();
            return true;
        } else if (item.getItemId() == 2) {
            Log.i("8086tiny", "Font Test requested");
            nativeTestFont8086();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }
    
    class EmulatorView extends SurfaceView implements SurfaceHolder.Callback {
        private SurfaceHolder holder;
        private Bitmap frameBitmap;
        private int[] frameBuffer;
        
        public EmulatorView(Context context) {
            super(context);
            holder = getHolder();
            holder.addCallback(this);
            setFocusable(true);
            setFocusableInTouchMode(true);
            requestFocus();
        }
        
        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            Log.i("8086tiny", "Surface created");
            // Recreate bitmap and buffer on surface creation (e.g., after app resume)
            this.holder = holder;
            frameBitmap = Bitmap.createBitmap(frameWidth, frameHeight, Bitmap.Config.ARGB_8888);
            frameBuffer = new int[frameWidth * frameHeight];
            for (int i = 0; i < frameBuffer.length; i++) {
                frameBuffer[i] = 0xFF000000;
            }
            frameBitmap.setPixels(frameBuffer, 0, frameWidth, 0, 0, frameWidth, frameHeight);
            Canvas canvas = holder.lockCanvas();
            if (canvas != null) {
                android.graphics.Rect src = new android.graphics.Rect(0, 0, frameWidth, frameHeight);
                android.graphics.Rect dst = new android.graphics.Rect(0, 0, getWidth(), getHeight());
                canvas.drawBitmap(frameBitmap, src, dst, null);
                holder.unlockCanvasAndPost(canvas);
                Log.i("8086tiny", "Surface recreated and initial fill drawn");
            }
        }
        
        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Log.i("8086tiny", "Surface changed: " + width + "x" + height);
            frameBitmap = Bitmap.createBitmap(frameWidth, frameHeight, Bitmap.Config.ARGB_8888);
            frameBuffer = new int[frameWidth * frameHeight];
            
            // Fill with black initially - the emulator will update with real content
            for (int i = 0; i < frameBuffer.length; i++) {
                frameBuffer[i] = 0xFF000000;
            }
            frameBitmap.setPixels(frameBuffer, 0, frameWidth, 0, 0, frameWidth, frameHeight);
            Canvas canvas = holder.lockCanvas();
            if (canvas != null) {
                android.graphics.Rect src = new android.graphics.Rect(0, 0, frameWidth, frameHeight);
                android.graphics.Rect dst = new android.graphics.Rect(0, 0, width, height);
                canvas.drawBitmap(frameBitmap, src, dst, null);
                holder.unlockCanvasAndPost(canvas);
                Log.i("8086tiny", "Initial surface fill drawn");
            }
        }
        
        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            Log.i("8086tiny", "Surface destroyed");
        }
        
        public void renderFrame() {
            if (frameBitmap == null) {
                return;
            }
            int[] fb = nativeGetFramebuffer8086();
            if (fb != null) {
                // Get current screen dimensions from native side
                int[] size = new int[2];
                nativeGetScreenSize8086(size);
                int nativeWidth = size[0];
                int nativeHeight = size[1];
                
                // Recreate bitmap if dimensions changed (e.g., text mode -> graphics mode)
                if (nativeWidth != frameWidth || nativeHeight != frameHeight) {
                    Log.i("8086tiny", "Screen size changed: " + frameWidth + "x" + frameHeight + " -> " + nativeWidth + "x" + nativeHeight);
                    frameWidth = nativeWidth;
                    frameHeight = nativeHeight;
                    frameBitmap = Bitmap.createBitmap(frameWidth, frameHeight, Bitmap.Config.ARGB_8888);
                    frameBuffer = new int[frameWidth * frameHeight];
                }
                
                if (fb.length == frameBuffer.length) {
                    System.arraycopy(fb, 0, frameBuffer, 0, fb.length);
                    frameBitmap.setPixels(frameBuffer, 0, frameWidth, 0, 0, frameWidth, frameHeight);
                    Canvas canvas = holder.lockCanvas();
                    if (canvas != null) {
                        // Scale framebuffer to fit screen while preserving aspect ratio
                        int screenW = getWidth();
                        int screenH = getHeight();
                        float scale = Math.min((float)screenW / frameWidth, (float)screenH / frameHeight);
                        int scaledW = Math.round(frameWidth * scale);
                        int scaledH = Math.round(frameHeight * scale);
                        int offsetX = (screenW - scaledW) / 2;
                        int offsetY = (screenH - scaledH) / 2;
                        
                        android.graphics.Rect dst = new android.graphics.Rect(
                            offsetX, offsetY, offsetX + scaledW, offsetY + scaledH);
                        android.graphics.Rect src = new android.graphics.Rect(0, 0, frameWidth, frameHeight);
                        canvas.drawBitmap(frameBitmap, src, dst, null);
                        holder.unlockCanvasAndPost(canvas);
                    }
                } else {
                    Log.i("8086tiny", "renderFrame: fb length mismatch, got " + fb.length + " expected " + frameBuffer.length);
                }
            }
        }
    }
}
