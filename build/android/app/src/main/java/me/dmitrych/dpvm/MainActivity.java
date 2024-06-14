package me.dmitrych.dpvm;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.BufferedOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;

import android.content.pm.PackageManager;
import android.content.Context;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.annotation.NonNull;

import android.os.Bundle;
import android.os.Handler;
import android.widget.TextView;
import android.widget.EditText;

import android.view.KeyEvent;
import android.view.inputmethod.EditorInfo;

import me.dmitrych.dpvm.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'dpvm' library on application startup.
    static {
        System.loadLibrary("dpvm");
    }

    private static final int STORAGE_PERMISSION_CODE = 101;
    private static final int maxTextHeight = 30;
    private static final int maxTextWidth = 35;
    private ActivityMainBinding binding;
    private Context myContext;
    private TextView tv;
    private EditText te;
    private String text = "";

    private int textHeight(String str) {
        int h = 1, i, w = 0;
        for (i = 0; i < str.length(); i++) {
            if (str.charAt(i) == '\n') {
                h ++; w = 0;
            } else if (w > maxTextWidth) {
                h ++; w = 1;
            } else {
                w++;
            }
        }
        return h;
    }

    private void out(String str) {
        if (str.length() > 0) {
            text += str;
            while (textHeight(text) > maxTextHeight) {
                int pos = text.indexOf('\n');
                if (pos < 0)
                    text = str;
                else
                    text = text.substring(pos + 1, text.length());
            }
            tv.setText(text);
        }
    }

    public boolean copyAsset(String path)
    {
        String to;
        String from = path;
        if (path.charAt(0) == '_')
            to = "." + path.substring(1, path.length());
        else
            to = path;

        try {
            File copied = new File(myContext.getFilesDir(), to);
            if (copied.exists()) {
                return true;
            }

            InputStream in = getResources().getAssets().open(from);
            OutputStream out = new BufferedOutputStream(new FileOutputStream(copied));

            byte[] buffer = new byte[0x100000];
            int lengthRead;
            while ((lengthRead = in.read(buffer)) > 0) {
                out.write(buffer, 0, lengthRead);
            }
            out.flush();
            out.close();
            in.close();
        } catch(IOException e) {
            out("Error: can't find asset " + e.getMessage() + "\n");
            return false;
        }
        return true;
    }

    public void mkdir(String path)
    {
        File theDir = new File(myContext.getFilesDir(), path);
        if (!theDir.exists()) {
            theDir.mkdirs();
        }
    }

    private boolean copyAssets(String path)
    {
        String [] list;
        try {
            list = getResources().getAssets().list(path);
            if (list.length > 0) {
                if (path.length() > 0)
                    mkdir(path);
                for (String file : list) {
                    String subpath = path.length() > 0 ? path + "/" + file : file;
                    if (!copyAssets(subpath))
                        return false;
                    else
                        copyAsset(subpath);
                }
            }
        } catch (IOException e) {
            out("Error: can't find asset folder " + e.getMessage() + "\n");
            return false;
        }

        return true;
    }

    public void run()
    {
        System.setProperty("user.dir", myContext.getFilesDir().getAbsolutePath());

        out("Copying assets to internal storage... ");

        if (!copyAssets(""))
            return;

        out("copied.\n");

        Handler timerHandler = new Handler();
        Runnable timerRunnable = new Runnable() {
            @Override
            public void run() {
                out(stringFromJNI(""));
                timerHandler.postDelayed(this, 100);
            }
        };

        timerHandler.postDelayed(timerRunnable, 0);

        te.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
                boolean handled = false;
                if (actionId == EditorInfo.IME_ACTION_DONE) {
                    out(te.getText().toString() + "\n");
                    out(stringFromJNI(te.getText().toString()));
                    handled = true;
                }
                return handled;
            }
        });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults)
    {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        if (grantResults.length >= 3
                && grantResults[0] == PackageManager.PERMISSION_GRANTED
                && grantResults[1] == PackageManager.PERMISSION_GRANTED
                && grantResults[2] == PackageManager.PERMISSION_GRANTED
        ) {
            out("Android permissions granted.\n");
        } else {
            out("Error: Android permissions denied.\n");
        }

        run();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        myContext = getApplicationContext();
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        tv = binding.sampleText;
        te = binding.commandText;

        if (
                    ContextCompat.checkSelfPermission(MainActivity.this,
                            android.Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED &&
                    ContextCompat.checkSelfPermission(MainActivity.this,
                            android.Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED &&
                    ContextCompat.checkSelfPermission(MainActivity.this,
                            android.Manifest.permission.INTERNET) == PackageManager.PERMISSION_GRANTED) {
             out("Android permissions already granted\n");
             run();
        } else {
            ActivityCompat.requestPermissions(MainActivity.this, new String[]
                    {
                            android.Manifest.permission.READ_EXTERNAL_STORAGE,
                            android.Manifest.permission.WRITE_EXTERNAL_STORAGE,
                            android.Manifest.permission.INTERNET
                    },
                    STORAGE_PERMISSION_CODE);
        }

    }

    /**
     * A native method that is implemented by the 'dpvm' native library,
     * which is packaged with this application.
     * @param s
     */
    public native String stringFromJNI(String s);
}