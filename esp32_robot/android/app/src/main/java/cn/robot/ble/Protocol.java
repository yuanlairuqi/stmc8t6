package cn.robot.ble;
import java.util.UUID;
public final class Protocol {
    public static final UUID SERVICE=UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    public static final UUID COMMAND=UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    public static final UUID STATUS=UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    public static final UUID CCCD=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    public static final String[] MOTION={"停止","正转","反转","保留","保留","本地动作"};
    public static final String[] ERRORS={"正常","指令错误","指令超时，已停止","连接断开，已停止","电压采样异常",
        "串口未配置","STM32 离线","STM32 拒绝或故障","串口发送失败","STM32 确认超时"};
    private static long u32(byte[] b,int n) {
        return ((long)b[n]&255)|((long)b[n+1]&255)<<8|((long)b[n+2]&255)<<16|((long)b[n+3]&255)<<24;
    }
    public static byte[] command(int motion,int speed,long seq) {
        return command(motion,speed,seq,0,2000,2000);
    }
    public static byte[] command(int motion,int speed,long seq,int mode,long forwardMs,long reverseMs) {
        if(motion<0 || motion>2 || speed<0 || speed>100 ||
            (motion==0 && speed!=0) || (motion!=0 && speed==0)||mode<0||mode>1||
            (mode==1&&(forwardMs<100||forwardMs>3600000||reverseMs<100||reverseMs>3600000))) throw new IllegalArgumentException("Invalid command");
        byte[] b=new byte[16];b[0]=3;b[1]=(byte)motion;b[2]=(byte)speed;b[3]=(byte)mode;
        for(int i=0;i<4;i++) b[4+i]=(byte)(seq >>> (8*i));
        for(int i=0;i<4;i++){b[8+i]=(byte)(forwardMs>>>(8*i));b[12+i]=(byte)(reverseMs>>>(8*i));}
        return b;
    }
    public static Status status(byte[] b) {
        if(b==null || b.length!=20 || b[0]!=3 || (b[1]&255)>5 || (b[5]&255)>2 ||
            (b[18]&255)>1||(b[19]&255)>2||((b[1]&255)>2&&b[1]!=5)||
            (b[6]&255)>100 || (b[7]&255)>9 || ((b[2]&2)!=0&&(b[2]&32)!=0) ||
            ((b[2]&8)==0&&(b[2]&(16|32|64))!=0)) return null;
        return new Status(b);
    }
    public static final class Status {
        public final int motion,commanded,flags,batteryMv,speed,error,runMode,phase;
        public final long ack,stmAck,uptime;
        Status(byte[] b) {
            motion=b[1]&255; flags=b[2]&255; batteryMv=(b[3]&255)|((b[4]&255)<<8);
            commanded=b[5]&255;speed=b[6]&255; error=b[7]&255; ack=u32(b,8);stmAck=u32(b,12);uptime=(b[16]&255)|((b[17]&255)<<8);runMode=b[18]&255;phase=b[19]&255;
        }
        public boolean demo(){return (flags&2)!=0;}
        public boolean ready(){return (flags&1)!=0;}
        public boolean batteryValid(){return (flags&4)!=0 && batteryMv!=65535;}
        public boolean uart(){return (flags&128)!=0;}
        public boolean stmOnline(){return (flags&8)!=0;}
        public boolean applied(){return (flags&32)!=0;}
        public boolean canMove(){return ready()&&motion!=5&&(!uart()?demo():stmOnline()&&(flags&16)!=0&&(flags&64)==0);}
    }
}
