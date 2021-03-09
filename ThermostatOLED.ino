
/*now A 2.5*12 v=30W = 20sec to 250C
 for 3.5v need 10-15A  для сравнения: в эл. сигар. сопротивление 0.1-0.4 Ом

 если будет нагреваться за 5-15 сек. отлично.

ESP https://tinkerman.cat/post/eeprom-rotation-for-esp8266-and-esp32/
*/
#define debug

#define LCD_active_only_on_input // if not LCD also will be active while play and rec  

#define LCD_upd_dt 40 //frame time
#define nextLCD_off_il_DT 20 //number of LCD updates, when LCD stay active after set nextLCD_off_il. Each upd cause nextLCD_off_il--;

#define HEAT_MM		255
#define th_p		A0
#include <Thermistor.h>
#include <NTC_Thermistor.h>
#define REFERENCE_RESISTANCE	10030
#define NOMINAL_RESISTANCE		100000
#define NOMINAL_TEMPERATURE		25
#define B_VALUE					3950


#define btn_next_T	6
#define btn_start	7
#define pw_p		3 //pwm: 3, 5, 6
#define fan_p		2 
Thermistor* thermistor;

#include <EEPROM.h>

#define tab	Serial.print("\t");


unsigned long lastMillis = 0;
//------------------------ 
//IIC ESP32 SDA=21 SCK=22
#define useOLED
#ifdef useOLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
//#include <splash.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

//for an SSD1306 display connected to I2C (SDA, SCL pins)
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1	//4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif
//------------------------
long next_btns_read_t=0;
long LCD_next_upd_t=0;

#define s0_NONE	0
#define s1_HEAT	1
#define s2_WAIT	2
#define s3_COOL	3
byte state=s0_NONE;


byte t_N=0;
//const uint16_t Ts[]={};
const uint16_t Ts[]={180,200,220,250};
//for test:  230,240 has different speed near target.  179,199, 249 does off LCD for save it and power
//const uint16_t Ts[]={179,180,199,200,220,230,240,249,250};


uint16_t s2_WAIT_while_hiT_dt=10000;
long s2_WAIT_start_t0;

uint16_t T_s3_COOL=40;
//uint16_t T_s3_COOL=21;
double T=0;
uint16_t T_need=0;

long LCD_next_off_t=10000;

float ta_avg=20;

bool bbtn_wait_off=false;
double dT=0;
double T_last=20;
long dT_next_check_t=1000;
bool bOverHeat=false;

double T_LCD_last=20;
long 	T_LCD_last_changed_t=0;
long 	s1_HEAT_start_t0=0;

void blink_p_E(byte p, byte E)
{
	for(byte i=0;i<E;i++)
	{
	digitalWrite(p, 1); delay(150);
	digitalWrite(p, 0); delay(150);
	}
}

byte Ts_size;
//============================================================================================================================
void setup() {
	delay(100);
  #ifdef debug
  Serial.begin(115200);
  #endif
  
  pinMode(pw_p, OUTPUT);
  pinMode(fan_p, OUTPUT);
  pinMode(btn_next_T, INPUT_PULLUP);
  pinMode(btn_start, INPUT_PULLUP);

  #ifdef useESP32
   			WiFi.mode( WIFI_OFF );
			WiFi.forceSleepBegin();
   pinMode(btns_p, INPUT_PULLUP);
  #endif

#ifdef useOLED
	if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
		#ifdef debug
		Serial.println(F("oled alloc fail"));
		#endif
	}
	#ifndef save_mem
	display.clearDisplay(); display.display();
	#endif
#endif

//analogReference(EXTERNAL);  //to use connected AREF to 3v3

thermistor = new NTC_Thermistor(
    th_p,
    REFERENCE_RESISTANCE,
    NOMINAL_RESISTANCE,
    NOMINAL_TEMPERATURE,
    B_VALUE
  );
  
  auto t=thermistor->readCelsius();
  
  if(t>300 || t<-30)
  {
	  #ifdef debug
	  Serial.print("thermistor err: ");
	  Serial.print(analogRead(th_p)); tab
	  Serial.println(t);
	  #endif
	  blink_p_E(LED_BUILTIN, 5);
  }
  if(!digitalRead(btn_next_T) && !digitalRead(btn_start))
  {
	  #ifdef debug
	  Serial.print("buttons have to be normal open: ");
	  Serial.print(digitalRead(btn_next_T)); tab
	  Serial.print(digitalRead(btn_start)); tab
	  Serial.println(t);
	  #endif
	  blink_p_E(LED_BUILTIN, 10);
  }
  
  Ts_size=sizeof(Ts)/sizeof(Ts[0]);
  t_N=EEPROM.read(1);
  t_N=constrain(t_N, 0, Ts_size-1);
}


void heat(){
 state=s1_HEAT;
 digitalWrite(fan_p, 0);
 s1_HEAT_start_t0=millis();
 
 EEPROM.update(1, t_N);
}
void wait(){
  state=s2_WAIT;
  s2_WAIT_start_t0=millis();
}
void cool(){
 state=s3_COOL;
 analogWrite(pw_p,0);
 digitalWrite(fan_p, 1);
}


void off(){
 state=s0_NONE;
 analogWrite(pw_p,0);
 digitalWrite(fan_p, 0);
}

void LCD_hold_for(uint16_t dt){
 LCD_next_off_t=millis()+dt;
}



void loop() {
	double T_tmp=0;
	#define AVG_E	2
	for(byte i=0; i<AVG_E;i++)
	{
		double t=thermistor->readCelsius();
		if(abs(T_last-t)>300) continue;		//err reading
		T_tmp+=t;
	}
	T=T*0.5+T_tmp/(2*AVG_E);
	if(millis()>dT_next_check_t)
	{
		dT_next_check_t=millis()+300;
		dT=T-T_last;
		T_last=T;
	}
																											#ifdef debug
																											//Serial.print(thermistor->readCelsius());tab
																											//Serial.print(T);tab
																											//Serial.println();
																											#endif
//---------------------------------------------- check states
	if(state==s1_HEAT || state==s2_WAIT) //thermostat
	{
		int pw;
		// if(Ts[t_N]<100)
		// {
			// pw=map(T, Ts[t_N]-10, Ts[t_N], 25, 0); //max use 10% power of heater
		// }
		// else
		{
			int power_M=255;

			if(Ts[t_N]==230)
			pw=map(T, Ts[t_N]-10, Ts[t_N]-1, power_M, 0); //TODO this for test
			if(Ts[t_N]==240)
			pw=map(T, Ts[t_N]-20, Ts[t_N]-1, power_M, 0);
			if(Ts[t_N]==250)
			{
				power_M=map(dT, 0, 5, 255, 5);
				power_M=constrain(power_M, 0, 255);
				
				if(T< Ts[t_N]-40) pw=255;
				else
				pw=map(T, Ts[t_N]-40, Ts[t_N]-1, power_M, 0);
			}
			else
			pw=map(T, Ts[t_N]-30, Ts[t_N]-1, power_M, 0);

			pw=constrain(pw, 0, power_M);
		}
		
		pw=constrain(pw, 0, 255);
		analogWrite(pw_p,pw);
		
		LCD_hold_for(10000);
	}	
	
	
	long wait_more;
	
	if(state==s1_HEAT)
	{
		if(T>=Ts[t_N]) wait();
	}
	else
	if(state==s2_WAIT)
	{
		wait_more= s2_WAIT_start_t0+s2_WAIT_while_hiT_dt- millis();
		if(wait_more<=0) cool();
	}
	else
	if(state==s3_COOL)
	{
		if(T<=T_s3_COOL) off();
		LCD_hold_for(1000);
	}
	
	
	//------------ overheat emergency cool
	 bool bOverHeat_now=T>HEAT_MM;
	 if(bOverHeat_now)
	 {
		analogWrite(pw_p,0);
		digitalWrite(fan_p, 1);
		bOverHeat=true;
	 }
	 else
		 if(bOverHeat)
		 {
			 digitalWrite(fan_p, 0);
			 bOverHeat=false;
		 }
	
//---------------------------------------------- UI
if(millis()>next_btns_read_t)
{
	if(bbtn_wait_off)
	{
		next_btns_read_t=millis()+50;
		if(digitalRead(btn_next_T) && digitalRead(btn_start)) bbtn_wait_off=false;
	}
	else
	{
		next_btns_read_t=millis()+100;
	
	if(!digitalRead(btn_next_T))
	{
		t_N++;
		if(t_N>=Ts_size) t_N=0;
		
		LCD_hold_for(15000);
		bbtn_wait_off=true;
	}
	if(!digitalRead(btn_start))
	{
		if(state==s0_NONE)
		{
			heat();
			next_btns_read_t=millis()+1000;
		}
		else
		{
			cool();
			next_btns_read_t=millis()+1000;
		}
		
		LCD_hold_for(15000);
		bbtn_wait_off=true;
	}
	}
}

//----------------------------------------------

//LCD_upd();
if(millis()>LCD_next_upd_t)
{
	LCD_next_upd_t=millis()+LCD_upd_dt;
	
	display.clearDisplay();
	
	if(millis()>LCD_next_off_t && (Ts[t_N]==179 || Ts[t_N]==199 || Ts[t_N]==249)) //off LCD only if selected: 179 199 249
	{
		display.display();
	}
	else
	{
		display.setTextSize(1);             // Normal 1:1 pixel scale
		display.setTextColor(SSD1306_WHITE);
		display.setCursor(0,0);             // Start at top-left corner
		display.print("select: ");	display.print(Ts[t_N]);
		
		switch(state)
		{
			case s0_NONE: display.print(" "); break;
			case s1_HEAT:
			display.print(" Heat ");
			
			display.print(((float)millis()-s1_HEAT_start_t0)/1000,1);
			break;
			case s2_WAIT:
			display.print(" Wait: ");
			//display.setCursor(80,21);
			display.print(((float)wait_more)/1000,1);
			break;
			case s3_COOL: display.print(" Cool"); break;
			default: display.print(" err"); break;
		}
		
		
		display.setCursor(0,11);
		display.print("T now : ");	
		if( //stabilize numbers on LCD
		 millis()>T_LCD_last_changed_t+500 || 
		(millis()>T_LCD_last_changed_t+200 && abs(T-T_LCD_last)>1)
		)
		{
			T_LCD_last_changed_t=millis();
			T_LCD_last=T;
			
			display.print(T,1);
		}
		else
			display.print(T_LCD_last,1);
		
		if(state==s1_HEAT || state==s3_COOL)
		{
			display.setCursor(0,22);
			display.print("T need: ");
			if(state==s1_HEAT)	display.print(Ts[t_N]);
			else
			if(state==s3_COOL)	display.print(T_s3_COOL);
		}

		display.display();
	}
}

}
