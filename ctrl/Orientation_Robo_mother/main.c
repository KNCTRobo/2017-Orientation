//	2017年度新入生オリエンテーション用制御回路プログラム

#include <16F886.h>
#fuses INTRC_IO,NOWDT,NOPROTECT,PUT,NOMCLR,NOLVP,BROWNOUT
#use delay(CLOCK=8000000)
#use fast_io(a)//先に宣言する。
#use fast_io(b)
#use fast_io(c)
#use fast_io(e)
#use RS232(BAUD=9600,RCV=PIN_C7,XMIT=PIN_C6,ERRORS,FORCE_SW)
	//XBeeからの受信->C7ピン  モータードライバへの送信->C6ピン
#byte port_a = 5
#byte port_b = 6
#byte port_c = 7
#byte port_e = 9

#byte INTCON = 0x0B
#include "ps_key_defines.h"

/*操作説明
	R1トリガ		- 誤操作防止ロック解除(押している間のみ操作できます)
	方向キー上下	- 前後移動
	方向キー左右	- 左右旋回
	○ボタン		- エアシリンダ駆動
	△ボタン		- ウインチ巻き上げ
	□ボタン		- ウインチ巻き戻し
*/
//設定項目--------------
#define MOTOR_MOVER	'R'
#define MOTOR_MOVEL	'L'
#define MOTOR_AIR	'A'
#define MOTOR_ROLL	'B'

#define BUFFER_SIZE 16

#define TIME_MOTOR_MARGIN 9000
	//モータードライバに信号を送信した直後の待ち時間
	//あまり小さい時間に設定しないこと!
#define F_TIME 200
	//LED試験点灯のLED点灯時間
#define MOTOR_LEVEL_R 50
#define MOTOR_LEVEL_L 50
#define MOTOR_LEVEL_WIND 100
	//モータードライバ出力レベル

#define RCV_THRESHOLD 3

#define LED_PULLUP 1
#define LED_OPR	PIN_A0
#define LED_F1	PIN_A2
#define EMITRULE_LED_F1 (rcv)&&(!(pwL||pwR||air||wind))
	//LED(緑)点灯の条件

#define ANALOG_THRESHOLD 30

//------------------

#define DIGITAL_MODE 0x41
#define ANALOG_MODE 0x73
	//アナログ・デジタルモード

char pad_mode=DIGITAL_MODE;	//最初にパッドの状態をデジタルにセットしておく

typedef struct {
	unsigned int X, Y;
} Point;
typedef struct {
	unsigned char sticks;
	Point depth_L;
	Point depth_R;
} Analogs;

Analogs gen_Analog(unsigned char source[],int offset){
/*/Analogs gen_Analog(int source[], int offset)
	//アナログスティックの計算
	Analogs の中身
		Analogs.sticks
		->	傾きの方向フラグ
			0x01 右スティック左
			0x02 右スティック右
			0x04 右スティック上
			0x08 右スティック下
			0x10 左スティック左
			0x20 左スティック右
			0x40 左スティック上
			0x80 左スティック下
		Analogs.depth_L
		->	左スティック傾き深度
		Analogs.depth_R
		->	右スティック傾き深度
/*/
	Analogs data;
	int StickRX;
	int StickRY;
	int StickLX;
	int StickLY;

	StickRX= source[4+offset];
	StickRY= source[5+offset];
	StickLX= source[6+offset];
	StickLY= source[7+offset];

	data.sticks= 0;
	data.depth_R.X= 0;
	data.depth_R.Y= 0;
	data.depth_L.X= 0;
	data.depth_L.Y= 0;

//--右X-
	if (StickRX < (0x80 - ANALOG_THRESHOLD)) {	//右Stickが左
		data.sticks += 0x01;
		data.depth_R.X = 0x7F - StickRX;
	} else if (StickRX > (0x80 + ANALOG_THRESHOLD)) {	//右Stickが右
		data.sticks += 0x02;
		data.depth_R.X = StickRX - 0x80;
	}
//--右Y-
	if (StickRY < (0x80 - ANALOG_THRESHOLD)) {	//右Stickが上
		data.sticks += 0x04;
		data.depth_R.Y = 0x7F - StickRY;
	} else if (StickRY > (0x80 + ANALOG_THRESHOLD)) {	//右Stickが下
		data.sticks += 0x08;
		data.depth_R.Y = StickRY - 0x80;
	}

//--左X-
	if (StickLX < (0x80 - ANALOG_THRESHOLD)) {	//左Stickが左
		data.sticks += 0x10;
		data.depth_L.X = 0x7F - StickLX;
	} else if (StickLX > (0x80 + ANALOG_THRESHOLD)) {	//左Stickが右
		data.sticks += 0x20;
		data.depth_L.X = StickLX - 0x80;
	}
//--左Y-
	if (StickLY < (0x80 - ANALOG_THRESHOLD)) {	//左Stickが上
		data.sticks += 0x40;
		data.depth_L.Y = 0x7F - StickLY;
	} else if (StickLY > (0x80 + ANALOG_THRESHOLD)) {	//左Stickが下
		data.sticks += 0x80;
		data.depth_L.Y = StickLY - 0x80;
	}
	return data;
}

void motor_emit(char channnel, int power){
	printf("%c%U*\r", channnel, power);
	delay_us(TIME_MOTOR_MARGIN);
}

void motor_reset(void){
	printf("A%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("B%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("C%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("D%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("E%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("F%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("L%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
	printf("R%U*\r",0);   delay_us(TIME_MOTOR_MARGIN);
}

void led_reset(void){
	output_low(LED_OPR);
	output_low(LED_F1);
}

void led_flash(void){
	output_high(LED_OPR);
	int i;
	for(i= 5; i; i--){
		output_high(LED_F1);
		delay_ms(200);
		output_low(LED_F1);
		delay_ms(200);
	}
}

void setup(void){	//初期化
	//入力ピンのセット
	set_tris_a(0xC0);
	set_tris_b(0xFF);
	//C7は受信に使用するため該当ビットを1にする
	set_tris_c(0x8D);//XMIT=6,RCV=7
	set_tris_e(0x00);

	//オシレータの初期化
	setup_oscillator(OSC_8MHZ);
}//初期化ルーチンおわり-----------------

void main(){
//メインルーチン
	RESET:	//ソフトリセット
//初期化
	setup();
	int
		i,	//Index変数
		fa =0, fb =0,	//fa =0, fb =0,	//Frame変数
		ofs =0, rcv =0
	;
	signed int
		pwA =0,	pwB =0,	pwC =0, pwD =0, pwR =0, pwL =0
			//モータードライバ出力レベル
	;
	signed int
		movev =0, mover =0, mode =0,
			//移動関連の情報を保持する変数
		armv =0, armh=0,
			//アームの昇降、開閉情報を保持する変数
		air =0,
			//エアシリンダの情報を格納する変数
		wind =0
			//ウインチの状態を格納する変数
	;
	unsigned char
		Data[BUFFER_SIZE],
			//PSコントローラからのデータを格納するバッファ
			//本来受信するデータは8Byteだが、あらかじめバッファを大きく設定しておく
		swstat =0,
			//リミットスイッチの情報を格納する変数
		motor_buf =0
	;
	long int rcv_err=RCV_THRESHOLD;
		//PSコントローラからの通信が途絶えたときに通信断絶状態と判定するための変数
		//閾値はRCV_THRESHOLDとする
	char RD;
		//PSコントローラデータ処理のためのバッファ
	Analogs Stick;
		//PSコントローラのアナログスティック情報

//モータードライバに0(出力0%)を送信(暴走を防ぐため)
	motor_reset();

//LED試験点灯
	led_flash();
	led_reset();
	delay_ms(500);

//メインループ
	while(1){	//無限ループ
	//PSコントローラからデータ受信
		for(i=0; i<BUFFER_SIZE; i++) Data[i]=0;
		gets(Data);
		if(1){
			for (i=0; i<BUFFER_SIZE; i++)
				//PSコントローラの識別子'Z'(0x5a)を捜索、オフセットを決定
				if (Data[i]=='Z') {
					ofs=i-1;
					break;
				};
		}

	//各変数をリフレッシュ
		pwA= 0;	pwB= 0;	pwC= 0;	pwD= 0;	pwL= 0;	pwR= 0;
		movev= 0;	mover= 0;
		armv= 0;	armh= 0;	mode= 0;
		air= 0;	wind= 0;		swstat= 0;
		fb++;	if(fb>=2) fb= 0;

	//PSコントローラのデータ処理
		if(Data[1+ofs]=='Z') {
			rcv_err=0;	rcv=1;
				//再試行回数のカウントをリセット、通信状態のフラグを1にする

			if(Data[2+ofs]&1) goto RESET;
				//SELECTキーが押されたらソフトリセット

			pad_mode= Data[0+ofs];

			if(Data[3+ofs]&(R1)){
				//安全装置としてR1を押している状態でないと動かないようにする
				RD= Data[2+ofs];
				movev=((RD&UP)?1:0) - ((RD&DOWN)?1:0);	//方向キー上下
				mover=((RD&LEFT)?1:0) - ((RD&RIGHT)?1:0);	//方向キー左右
				RD= Data[3+ofs];
				air=(RD&CIR)?1:0;
				wind=((RD&TRI)?1:0) - ((RD&SQU)?1:0);
			}
		} else {
		//PSコントローラから信号を受信できなかった時の処理
			if(rcv_err>=RCV_THRESHOLD) {
			//再試行回数が閾値を超えた場合通信断絶と判断する
				rcv=0;
				pad_mode=DIGITAL_MODE;
			}
			//再試行回数のカウント
			else rcv_err++;
		}

	//入力内容からデータを処理

		if(movev!=0) {
			pwL= MOTOR_LEVEL_L * movev;
			pwR= MOTOR_LEVEL_R * movev;
			if(mover>0) pwL= 0;
			else if(mover<0) pwR= 0;
		} else if(mover!=0){
			pwL= MOTOR_LEVEL_L * (-mover);
			pwR= MOTOR_LEVEL_R * mover;
		} else {
			pwL= 0;
			pwR= 0;
		}
		pwA= air? 100: 0;
		pwB= wind? MOTOR_LEVEL_WIND*wind: 0;


	//LED点灯制御
		#if LED_PULLUP !=0
		if(fb)
			output_low(LED_OPR);
		else
			output_high(LED_OPR);
		if(EMITRULE_LED_F1)
			output_low(LED_F1);
		else
			output_high(LED_F1);
		#else
		if(fb)
			output_high(LED_OPR);
		else
			output_low(LED_OPR);
		if(EMITRULE_LED_F1)
			output_high(LED_F1);
		else
			output_low(LED_F1);
		#endif

	//モータードライバの出力内容を決定、信号を出力する
		if(rcv && Data[3+ofs]&(R1)){
			if(movev || mover || (motor_buf&0x01)){
				motor_emit(MOTOR_MOVEL, pwL);
				motor_emit(MOTOR_MOVER, pwR);
				motor_buf= (movev||mover)?motor_buf|0x01:motor_buf&(0xFF-0x01);
			}
			if(air || (motor_buf&0x02))
			{
				motor_emit(MOTOR_AIR, pwA);
				motor_emit(MOTOR_ROLL, pwB);
				motor_buf= (air||wind)?motor_buf|0x02:motor_buf&(0xFF-0x02);
			}
		} else {
			motor_emit(MOTOR_MOVEL, 0);
			motor_emit(MOTOR_MOVER, 0);
		}

	}	//無限ループおわり
}	//メインルーチンおわり--------------