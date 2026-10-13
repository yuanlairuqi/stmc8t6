package cn.robot.ble;
import java.util.ArrayDeque;
public final class CommandChannel {
    public static final long ACK_MS=1000, WRITE_MS=1500;
    public static final class Packet {
        public final long sequence, createdAt;
        public final int motion;
        public final byte[] bytes;
        public long sentAt;
        Packet(long seq,int op,int speed,long now,int mode,long forwardMs,long reverseMs){sequence=seq;motion=op;createdAt=now;bytes=Protocol.command(op,speed,seq,mode,forwardMs,reverseMs);}
    }
    private final ArrayDeque<Packet> unconfirmed=new ArrayDeque<>();
    private Packet pending,writing;
    private long nextSequence,stopSequence;
    private int mode;private long forwardMs=2000,reverseMs=2000;
    public void parameters(int m,long f,long r){mode=m;forwardMs=f;reverseMs=r;}
    private boolean stopBarrier,synchronizedOnce,timeoutReported;
    private long sequence(){nextSequence=(nextSequence+1)&0xffffffffL;return nextSequence;}
    public void reset(){pending=null;writing=null;unconfirmed.clear();nextSequence=0;stopSequence=0;stopBarrier=false;synchronizedOnce=false;timeoutReported=false;}
    public boolean movement(int op,int speed,long now){
        if(op==0||!synchronizedOnce||stopBarrier)return false;
        pending=new Packet(sequence(),op,speed,now,mode,forwardMs,reverseMs);return true;
    }
    public void stop(long now){
        java.util.Iterator<Packet> it=unconfirmed.iterator();while(it.hasNext())if(it.next().motion!=0)it.remove();
        if(stopBarrier&&!timeoutReported)return;
        pending=null;
        stopBarrier=true;timeoutReported=false;stopSequence=sequence();
        pending=new Packet(stopSequence,0,0,now,0,0,0);
    }
    public Packet take(long now){
        if(writing!=null||pending==null)return null;
        if(pending.motion!=0&&(now-pending.createdAt>300||unconfirmed.size()>=4)){pending=null;return null;}
        writing=pending;pending=null;writing.sentAt=now;unconfirmed.addLast(writing);return writing;
    }
    public void written(){writing=null;}
    public boolean writingExpired(long now){return writing!=null&&now-writing.sentAt>=WRITE_MS;}
    public int acknowledgementExpired(long now){
        if(timeoutReported||unconfirmed.isEmpty()||now-unconfirmed.peekFirst().sentAt<ACK_MS)return -1;
        timeoutReported=true;return unconfirmed.peekFirst().motion;
    }
    public void status(Protocol.Status s){
        long ack=s.uart()?s.stmAck:s.ack;
        boolean known=false;for(Packet p:unconfirmed)if(p.sequence==ack){known=true;break;}
        if(!known)return; // Old-session, duplicate and invented future ACKs cannot clear the barrier.
        boolean validStop=ack==stopSequence&&s.ack==stopSequence&&s.motion==0&&s.commanded==0&&
            (!s.uart()||(s.stmOnline()&&(s.flags&64)==0));
        if(stopBarrier&&ack==stopSequence&&!validStop)return;
        while(!unconfirmed.isEmpty()){Packet p=unconfirmed.removeFirst();if(p.sequence==ack)break;}
        if(stopBarrier&&validStop) {
            stopBarrier=false;synchronizedOnce=true;timeoutReported=false;
        }
    }
    public boolean synchronizedOnce(){return synchronizedOnce;}
    public boolean stopping(){return stopBarrier;}
    public boolean idle(){return pending==null&&writing==null&&unconfirmed.isEmpty()&&!stopBarrier;}
    public boolean stopUnconfirmedTimedOut(){return stopBarrier&&timeoutReported;}
}
