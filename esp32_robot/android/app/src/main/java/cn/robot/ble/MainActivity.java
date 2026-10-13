package cn.robot.ble;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.util.*;

public final class MainActivity extends Activity {
    private final Handler ui=new Handler(Looper.getMainLooper());
    private static final int INK=0xff172b36, MUTED=0xff647680, ACCENT=0xff087f8c;
    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private ScanCallback scanCallback;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic tx;
    private boolean ready, subscribed, scanning, controlsAvailable, firstStatus;
    private int held, speed=40, generation, scanGeneration, runMode;
    private EditText forwardTime,reverseTime;
    private final CommandChannel channel=new CommandChannel();
    private long lastStatusAt,lastRssiAt,rssiStartedAt;
    private boolean rssiPending;
    private int rssi;
    private TextView connection, battery, state, confirmation, uptime, mode, log, speedLabel;
    private Button connectButton;
    private final ArrayList<Button> motionButtons=new ArrayList<>();
    private final LinkedHashMap<String,BluetoothDevice> devices=new LinkedHashMap<>();
    private ArrayAdapter<String> deviceList;
    private AlertDialog deviceDialog;
    private final ArrayDeque<String> logLines=new ArrayDeque<>();
    private final Runnable watchdog=new Runnable(){public void run(){
        long now=SystemClock.elapsedRealtime();
        if(subscribed && firstStatus && now-lastStatusAt>1500){fail("状态数据超时，请重新连接");}
        else if(channel.writingExpired(now)){fail("指令写入超时，请重新连接");}
        else if(rssiPending&&now-rssiStartedAt>1000){fail("蓝牙链路读取超时，请重新连接");}
        else {
            int expired=channel.acknowledgementExpired(now);
            if(expired>0){event("运动指令确认超时，已发送停止");release();}
            else if(expired==0){controlsAvailable=false;setControls(false);event("停止尚未获设备确认，运动已锁定");}
        }
        ui.postDelayed(this,100);
    }};
    private final Runnable repeat=new Runnable(){public void run(){
        if(held!=0 && controlsAvailable){send(held); ui.postDelayed(this,200);}
    }};
    @Override public void onCreate(Bundle b){
        super.onCreate(b);
        BluetoothManager manager=(BluetoothManager)getSystemService(BLUETOOTH_SERVICE);
        adapter=manager==null?null:manager.getAdapter();
        buildUI(); ui.post(watchdog);
        if(adapter==null) connection.setText("此手机不支持蓝牙");
    }
    private int dp(int n){return (int)(n*getResources().getDisplayMetrics().density+0.5f);}
    private GradientDrawable background(int color,int radius){
        GradientDrawable d=new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(radius)); return d;
    }
    private TextView text(String s,int size,int color,boolean bold){
        TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(color);
        if(bold)t.setTypeface(null,Typeface.BOLD);t.setPadding(0,dp(4),0,dp(4));return t;
    }
    private LinearLayout card(LinearLayout root){
        LinearLayout c=new LinearLayout(this);c.setOrientation(1);c.setPadding(dp(18),dp(14),dp(18),dp(14));
        c.setBackground(background(Color.WHITE,20));
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=dp(14);root.addView(c,p);return c;
    }
    private Button button(String label,int color){
        Button b=new Button(this);b.setText(label);b.setTextSize(16);b.setTextColor(Color.WHITE);
        b.setAllCaps(false);b.setMinHeight(dp(60));b.setMinimumHeight(dp(60));b.setPadding(dp(8),dp(6),dp(8),dp(6));
        b.setBackground(background(color,14));return b;
    }
    private void buildUI(){
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(0xfff3f5f7);
        LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(dp(22),dp(18),dp(22),dp(28));
        scroll.addView(root);setContentView(scroll);
        // Android 15 edge-to-edge: apply system-bar insets to the scroll viewport.
        scroll.setOnApplyWindowInsetsListener((v,insets)->{
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets x=insets.getInsets(WindowInsets.Type.systemBars());v.setPadding(x.left,x.top,x.right,x.bottom);}
            else v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            return insets;
        });scroll.requestApplyInsets();
        root.addView(text("电机控制台",28,INK,true));root.addView(text("手机 → ESP32 → STM32",13,MUTED,false));
        LinearLayout link=card(root);
        connection=text("未连接",19,INK,true);link.addView(connection);
        connectButton=button("搜索设备",ACCENT);link.addView(connectButton,new LinearLayout.LayoutParams(-1,dp(52)));
        connectButton.setOnClickListener(v->{if(gatt!=null){release();disconnect("已断开连接");}else startScan();});
        LinearLayout telemetry=card(root);
        mode=text("连接后显示设备状态",13,ACCENT,true);telemetry.addView(mode);
        LinearLayout row=new LinearLayout(this);telemetry.addView(row);
        LinearLayout voltage=new LinearLayout(this);voltage.setOrientation(1);
        voltage.addView(text("电池电压",12,MUTED,false));battery=text("— V",30,INK,true);voltage.addView(battery);
        LinearLayout running=new LinearLayout(this);running.setOrientation(1);
        running.addView(text("设备状态",12,MUTED,false));state=text("等待连接",20,INK,true);running.addView(state);
        row.addView(voltage,new LinearLayout.LayoutParams(0,-2,1));row.addView(running,new LinearLayout.LayoutParams(0,-2,1));
        confirmation=text("尚未收到设备确认",13,MUTED,false);telemetry.addView(confirmation);
        uptime=text("运行时长 —",12,MUTED,false);telemetry.addView(uptime);
        LinearLayout control=card(root);control.addView(text("电机控制",18,INK,true));
        control.addView(text("点击启动，停止按钮结束运行",13,MUTED,false));
        Spinner modes=new Spinner(this);modes.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"持续运行","定时交替正反转"}));control.addView(modes);
        LinearLayout times=new LinearLayout(this);control.addView(times);
        forwardTime=durationField(times,"正转时长（秒）");reverseTime=durationField(times,"反转时长（秒）");
        times.setVisibility(View.GONE);
        modes.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
            public void onItemSelected(AdapterView<?> p,View v,int n,long id){if(n!=runMode)release();runMode=n;times.setVisibility(n==1?View.VISIBLE:View.GONE);}
            public void onNothingSelected(AdapterView<?> p){}
        });
        speedLabel=text("速度 40%",14,INK,false);control.addView(speedLabel);
        SeekBar seek=new SeekBar(this);seek.setMax(100);seek.setProgress(40);control.addView(seek);
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar b,int n,boolean user){speed=n;speedLabel.setText("速度 "+speed+"%");if(user&&speed==0)release();}
            public void onStartTrackingTouch(SeekBar b){} public void onStopTrackingTouch(SeekBar b){}
        });
        LinearLayout directions=new LinearLayout(this);control.addView(directions);addMotion(directions,"正转启动",1);addMotion(directions,"反转启动",2);
        LinearLayout middle=new LinearLayout(this);control.addView(middle);
        Button stop=button("停止",0xffd34b46);addCell(middle,stop);stop.setOnClickListener(v->release());
        control.addView(text("交替模式从所选方向开始；两段时长 0.1–3600 秒",12,MUTED,false));
        LinearLayout events=card(root);events.addView(text("连接记录",14,INK,true));log=text("等待连接 ESP32-Robot",12,MUTED,false);events.addView(log);
        root.addView(text("退出页面或连接中断后自动停止控制",12,MUTED,false));setControls(false);
    }
    private EditText durationField(LinearLayout row,String label){
        LinearLayout col=new LinearLayout(this);col.setOrientation(1);row.addView(col,new LinearLayout.LayoutParams(0,-2,1));
        col.addView(text(label,12,MUTED,false));EditText field=new EditText(this);field.setSingleLine(true);field.setText("2.0");
        field.setInputType(android.text.InputType.TYPE_CLASS_NUMBER|android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);col.addView(field);return field;
    }
    private long duration(EditText field){double s=Double.parseDouble(field.getText().toString());if(Double.isNaN(s)||Double.isInfinite(s)||s<0.1||s>3600)throw new IllegalArgumentException();return Math.round(s*1000); }
    private void addCell(LinearLayout row,View v){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,dp(62),1);p.setMargins(dp(3),dp(3),dp(3),dp(3));row.addView(v,p);}
    private void addMotion(LinearLayout row,String label,int motion){
        Button b=button(label,ACCENT);addCell(row,b);motionButtons.add(b);
        b.setOnClickListener(v->{
            if(!controlsAvailable||channel.stopping())return;
            if(speed==0){event("请先设置大于 0 的速度");return;}
            try{channel.parameters(runMode,runMode==1?duration(forwardTime):0,runMode==1?duration(reverseTime):0);}
            catch(RuntimeException ex){event("时长请输入 0.1–3600 秒");return;}
            held=motion;forwardTime.setEnabled(false);reverseTime.setEnabled(false);ui.removeCallbacks(repeat);ui.post(repeat);
        });

    }
    private void setControls(boolean enabled){for(Button b:motionButtons){b.setEnabled(enabled);b.setAlpha(enabled?1:0.35f);}}
    private void event(String s){
        if(logLines.size()==5)logLines.removeFirst();logLines.addLast(s);StringBuilder b=new StringBuilder();
        for(String line:logLines){if(b.length()>0)b.append('\n');b.append(line);}log.setText(b.toString());
    }
    private boolean permissions(){
        String[] needed=Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        for(String p:needed)if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED){requestPermissions(needed,10);return false;}return true;
    }
    @Override public void onRequestPermissionsResult(int request,String[] p,int[] results){
        super.onRequestPermissionsResult(request,p,results);if(request!=10)return;
        for(int r:results)if(r!=PackageManager.PERMISSION_GRANTED){event("请在设置中允许蓝牙/附近设备权限");return;}
        if(results.length>0)startScan();
    }
    private void startScan(){
        if(adapter==null){event("此手机没有蓝牙适配器");return;}
        if(!permissions())return;
        if(!adapter.isEnabled()){startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));event("开启蓝牙后再次点击搜索设备");return;}
        stopScan();devices.clear();scanner=adapter.getBluetoothLeScanner();
        if(scanner==null){event("蓝牙扫描不可用");return;}
        deviceList=new ArrayAdapter<>(this,android.R.layout.simple_list_item_1,new ArrayList<>());
        final int token=++scanGeneration;
        deviceDialog=new AlertDialog.Builder(this).setTitle("选择 ESP32 设备").setAdapter(deviceList,(d,n)->{
            ArrayList<BluetoothDevice> list=new ArrayList<>(devices.values());stopScan();if(n<list.size())connect(list.get(n));
        }).setNegativeButton("取消",(d,w)->stopScan()).create();
        deviceDialog.setOnDismissListener(d->stopScan());deviceDialog.show();
        scanCallback=new ScanCallback(){
            @Override public void onScanResult(int type,ScanResult result){ui.post(()->{
                if(!scanning||token!=scanGeneration)return;
                BluetoothDevice d=result.getDevice();String key=d.getAddress();
                if(!devices.containsKey(key)){devices.put(key,d);String name=result.getScanRecord()==null?null:result.getScanRecord().getDeviceName();
                    deviceList.add((name==null?"ESP32-Robot":name)+"\n"+key+" · "+result.getRssi()+" dBm");}
            });}
            @Override public void onScanFailed(int error){ui.post(()->{if(token==scanGeneration){stopScan();connection.setText("搜索失败");event("扫描失败："+error);}});}
        };
        try{
            scanning=true;connection.setText("正在搜索…");
            scanner.startScan(Collections.singletonList(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(Protocol.SERVICE)).build()),
                new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
            ui.postDelayed(()->{if(token==scanGeneration && scanning){stopScan();connection.setText(devices.isEmpty()?"未找到设备":"请选择设备");
                event(devices.isEmpty()?"未找到设备：检查固件、电源；旧版安卓需开启定位":"搜索结束，点击列表中的设备");}},10000);
        }catch(RuntimeException e){stopScan();event("无法扫描："+e.getClass().getSimpleName());}
    }
    private void stopScan(){if(scanning&&scanner!=null&&scanCallback!=null){try{scanner.stopScan(scanCallback);}catch(RuntimeException ignored){}}scanning=false;}
    private void connect(BluetoothDevice device){
        disconnect("正在连接…");final int token=++generation;
        channel.reset();connectButton.setText("断开连接");event("连接 "+device.getAddress());
        try{gatt=device.connectGatt(this,false,callback,BluetoothDevice.TRANSPORT_LE);
            if(gatt==null){disconnect("无法创建连接");return;}
            ui.postDelayed(()->{if(token==generation && !firstStatus)fail("连接超时，请检查 ESP32");},15000);
        }catch(RuntimeException e){disconnect("连接失败");event(e.getClass().getSimpleName());}
    }
    private void release(){held=0;if(forwardTime!=null){forwardTime.setEnabled(true);reverseTime.setEnabled(true);}ui.removeCallbacks(repeat);if(ready){channel.stop(SystemClock.elapsedRealtime());controlsAvailable=false;setControls(false);pump();}}
    private void send(int motion){if(!ready||gatt==null||tx==null)return;
        if(motion==0)channel.stop(SystemClock.elapsedRealtime());else channel.movement(motion,speed,SystemClock.elapsedRealtime());pump();
    }
    private void pump(){
        if(!ready||gatt==null||tx==null||rssiPending)return;
        CommandChannel.Packet packet=channel.take(SystemClock.elapsedRealtime());if(packet==null)return;
        byte[] data=packet.bytes;
        boolean started;
        try{
            if(Build.VERSION.SDK_INT>=33)started=gatt.writeCharacteristic(tx,data,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
            else{tx.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);tx.setValue(data);started=gatt.writeCharacteristic(tx);}
        }catch(RuntimeException e){started=false;}
        if(!started){fail("指令发送失败");}
    }
    private void fail(String message){event(message);disconnect(message);}
    private void disconnect(String message){
        forwardTime.setEnabled(true);reverseTime.setEnabled(true);
        ++generation;held=0;ui.removeCallbacks(repeat);ready=false;subscribed=false;controlsAvailable=false;firstStatus=false;channel.reset();tx=null;rssi=0;lastRssiAt=0;rssiPending=false;
        setControls(false);connectButton.setText("搜索设备");connection.setText(message);connection.setTextColor(INK);
        battery.setText("— V");state.setText("等待连接");confirmation.setText("尚未收到设备确认");
        uptime.setText("运行时长 —");mode.setText("连接后显示设备状态");
        BluetoothGatt old=gatt;gatt=null;
        if(old!=null){try{old.disconnect();}catch(RuntimeException ignored){}old.close();}
    }
    private void receive(BluetoothGatt source,byte[] bytes){
        if(source!=gatt||!subscribed)return;Protocol.Status s=Protocol.status(bytes);
        if(s==null){fail("设备状态格式不匹配");return;}
        long now=SystemClock.elapsedRealtime();lastStatusAt=now;
        boolean initial=!firstStatus;firstStatus=true;ready=s.ready();channel.status(s);
        boolean previous=controlsAvailable;controlsAvailable=s.canMove()&&channel.synchronizedOnce()&&!channel.stopping()&&!channel.stopUnconfirmedTimedOut();
        if(previous&&!controlsAvailable&&held!=0)release();setControls(controlsAvailable);
        connection.setText("● ESP32 已连接"+(rssi==0?"":" · "+rssi+" dBm"));connection.setTextColor(ACCENT);
        mode.setText(!s.uart()?"BLE 演示 · UART 待配置":!s.stmOnline()?"STM32 离线 · 运动锁定":
            s.motion==5?"STM32 局部动作 · 连续控制锁定":s.demo()?"STM32 通信演示 · 驱动待接入":
            (s.flags&64)!=0?"STM32 故障 · 运动锁定":(s.flags&16)==0?"STM32 在线 · 连续控制未支持":"STM32 在线 · 双向通信正常");
        battery.setText(s.batteryValid()?String.format(Locale.ROOT,"%.2f V",s.batteryMv/1000.0):"未接入");
        battery.setTextSize(s.batteryValid()?30:22);
        state.setText(!s.uart()?"演示："+Protocol.MOTION[s.motion]:!s.stmOnline()?"未知":
            s.demo()?"演示："+Protocol.MOTION[s.motion]:"回报："+Protocol.MOTION[s.motion]);
        confirmation.setText("ESP32 已收 #"+s.ack+"\n"+(!s.uart()?"STM32 待配置":!s.stmOnline()?"STM32 确认不可用":
            "STM32 已收 #"+s.stmAck+(s.applied()?" · 输出已应用":" · 输出未确认"))+(channel.stopping()?"\n正在确认停止":""));
        uptime.setText((s.runMode==1?"定时交替":"持续运行")+" · 指令速度 "+s.speed+"% · "+Protocol.ERRORS[s.error]);
        if(initial&&ready){event("状态接收正常，先同步停止状态");send(0);}
        pump();
        // RSSI is advisory; avoid overlap with a queued/inflight GATT write.
        if(now-lastRssiAt>=3000&&held==0&&channel.idle()&&!rssiPending){
            lastRssiAt=now;rssiPending=source.readRemoteRssi();rssiStartedAt=now;
        }
    }
    private final BluetoothGattCallback callback=new BluetoothGattCallback(){
        @Override public void onConnectionStateChange(BluetoothGatt g,int status,int newState){ui.post(()->{
            if(g!=gatt){g.close();return;}
            if(status!=BluetoothGatt.GATT_SUCCESS||newState==BluetoothProfile.STATE_DISCONNECTED){event("蓝牙连接断开："+status);disconnect("连接已断开");}
            else if(newState==BluetoothProfile.STATE_CONNECTED){connection.setText("正在读取服务…");if(!g.discoverServices())fail("服务发现失败");}
        });}
        @Override public void onServicesDiscovered(BluetoothGatt g,int status){ui.post(()->{
            if(g!=gatt)return;if(status!=BluetoothGatt.GATT_SUCCESS){fail("读取服务失败");return;}
            BluetoothGattService service=g.getService(Protocol.SERVICE);
            if(service==null){fail("设备固件不匹配");return;}
            tx=service.getCharacteristic(Protocol.COMMAND);BluetoothGattCharacteristic rx=service.getCharacteristic(Protocol.STATUS);
            if(tx==null||rx==null||(tx.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)==0||
                (rx.getProperties()&BluetoothGattCharacteristic.PROPERTY_NOTIFY)==0){fail("缺少通信特征");return;}
            BluetoothGattDescriptor d=rx.getDescriptor(Protocol.CCCD);
            if(d==null||!g.setCharacteristicNotification(rx,true)){fail("无法订阅状态");return;}
            boolean started;
            if(Build.VERSION.SDK_INT>=33)started=g.writeDescriptor(d,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS;
            else{d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);started=g.writeDescriptor(d);}
            if(!started)fail("状态订阅失败");
        });}
        @Override public void onDescriptorWrite(BluetoothGatt g,BluetoothGattDescriptor d,int status){ui.post(()->{
            if(g!=gatt)return;if(status!=BluetoothGatt.GATT_SUCCESS){fail("状态订阅被拒绝");return;}
            subscribed=true;connection.setText("等待设备状态…");
        });}
        @Override public void onCharacteristicWrite(BluetoothGatt g,BluetoothGattCharacteristic c,int status){ui.post(()->{
            if(g!=gatt)return;channel.written();if(status!=BluetoothGatt.GATT_SUCCESS){fail("ESP32 拒绝指令："+status);return;}pump();
        });}
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c){
            byte[] data=c.getValue();if(data!=null){byte[] copy=data.clone();ui.post(()->{if(Protocol.STATUS.equals(c.getUuid()))receive(g,copy);});}
        }
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] value){
            byte[] copy=value.clone();ui.post(()->{if(Protocol.STATUS.equals(c.getUuid()))receive(g,copy);});
        }
        @Override public void onReadRemoteRssi(BluetoothGatt g,int value,int status){ui.post(()->{
            if(g!=gatt)return;rssiPending=false;if(status==BluetoothGatt.GATT_SUCCESS)rssi=value;pump();
        });}
    };
    @Override protected void onPause(){release();stopScan();if(deviceDialog!=null)deviceDialog.dismiss();disconnect("已停止控制");super.onPause();}
    @Override protected void onDestroy(){ui.removeCallbacksAndMessages(null);super.onDestroy();}
}
