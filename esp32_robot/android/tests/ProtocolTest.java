import cn.robot.ble.Protocol;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
public class ProtocolTest {
    public static void main(String[] args) throws Exception {
        for(int op=0;op<=2;op++) {
            byte[] c=Protocol.command(op,op==0?0:40,0x12345678L);
            assert c.length==16&&c[0]==3&&c[1]==op&&c[4]==0x78&&c[7]==0x12;
        }
        assert Protocol.command(1,1,0xffffffffL)[7]==(byte)0xff;
        byte[] cycle=Protocol.command(1,55,42,1,100,3600000);
        assert cycle[3]==1&&cycle[8]==100&&cycle[12]==(byte)0x80&&cycle[13]==(byte)0xee&&cycle[14]==0x36;
        for(long duration:new long[]{0,99,3600001}){
            boolean threw=false;try{Protocol.command(1,40,42,1,duration,2000);}catch(IllegalArgumentException e){threw=true;}assert threw;
        }
        for(int[] invalid:new int[][]{{5,40},{1,101},{1,0},{0,10},{-1,30}}){
            boolean threw=false;try{Protocol.command(invalid[0],invalid[1],1);}catch(IllegalArgumentException e){threw=true;}assert threw;
        }
        assert Protocol.status(null)==null;assert Protocol.status(new byte[19])==null;
        byte[] b=Files.readAllBytes(Paths.get(args[0]));Protocol.Status s=Protocol.status(b);
        assert s!=null&&!s.demo()&&s.ready()&&s.batteryValid()&&s.uart()&&s.stmOnline()&&s.applied()&&s.canMove();
        assert s.batteryMv==7400&&s.motion==2&&s.speed==75&&s.ack==0x12345678L&&s.stmAck==0x12345678L&&s.uptime==12;
        byte[] wrong=Arrays.copyOf(b,20);wrong[0]=1;assert Protocol.status(wrong)==null;
        wrong=Arrays.copyOf(b,20);wrong[1]=6;assert Protocol.status(wrong)==null;
        wrong=Arrays.copyOf(b,20);wrong[1]=5;assert !Protocol.status(wrong).canMove();
        wrong=Arrays.copyOf(b,20);wrong[6]=101;assert Protocol.status(wrong)==null;
        wrong=Arrays.copyOf(b,20);wrong[7]=10;assert Protocol.status(wrong)==null;
        wrong=Arrays.copyOf(b,20);wrong[2]=3;wrong[3]=(byte)255;wrong[4]=(byte)255;assert !Protocol.status(wrong).batteryValid();
        wrong=Arrays.copyOf(b,20);wrong[2]=(byte)129;assert !Protocol.status(wrong).canMove();
        wrong=Arrays.copyOf(b,20);wrong[2]=(byte)(s.flags|2);assert Protocol.status(wrong)==null;
        System.out.println("PASS: Android codec + actual C-generated telemetry match; malformed versions/ranges rejected");
    }
}
