#include "i2c.h"
#include "FT6336.h"
//EPD
#include <SPI.h>
#include "Ap_29demo.h"
//IO settings

#define EPD_WIDTH 480
#define EPD_HEIGHT 800
#define EPD_ARRAY EPD_WIDTH *EPD_HEIGHT / 8

// ---- Touch disabled for now: only the e-paper is wired up ----
// (IRQ_Pin / Is_INT_IN / FT6336_Init() are not used below)

//IO settings -- matches wiring: BUSY->D4, RST->RX2(D16), DC->TX2(D17), CS->D5
#define isEPD_W21_BUSY digitalRead(4)  //BUSY

#define EPD_W21_RST_0 digitalWrite(16, LOW)
#define EPD_W21_RST_1 digitalWrite(16, HIGH)

#define EPD_W21_DC_0 digitalWrite(17, LOW)
#define EPD_W21_DC_1 digitalWrite(17, HIGH)

#define EPD_W21_CS_0 digitalWrite(5, LOW)
#define EPD_W21_CS_1 digitalWrite(5, HIGH)

#define EPD_W21_MOSI_0 digitalWrite(23, LOW)  //SDA
#define EPD_W21_MOSI_1 digitalWrite(23, HIGH)

#define EPD_W21_CLK_0 digitalWrite(18, LOW)  //SCL
#define EPD_W21_CLK_1 digitalWrite(18, HIGH)

#define EPD_W21_READ digitalRead(23)  //SDA
////////FUNCTION//////

void SPI_Write(unsigned char value);
void EPD_W21_WriteDATA(unsigned char datas);
void EPD_W21_WriteCMD(unsigned char command);
unsigned char EPD_W21_ReadDATA(void);
//EPD

//Full screen refresh display
void EPD_HW_Init(void);
void EPD_HW_Init_180(void);
void EPD_WhiteScreen_ALL(const unsigned char *datas);
void EPD_WhiteScreen_White(void);
void EPD_WhiteScreen_Black(void);
void EPD_DeepSleep(void);
//Partial refresh display
void EPD_SetRAMValue_BaseMap(const unsigned char *datas);
void EPD_Dis_PartAll(const unsigned char *datas);
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start, const unsigned char *datas, unsigned int PART_COLUMN, unsigned int PART_LINE);
void EPD_Dis_Part_Time(unsigned int x_startA, unsigned int y_startA, const unsigned char *datasA,
                       unsigned int x_startB, unsigned int y_startB, const unsigned char *datasB,
                       unsigned int x_startC, unsigned int y_startC, const unsigned char *datasC,
                       unsigned int x_startD, unsigned int y_startD, const unsigned char *datasD,
                       unsigned int x_startE, unsigned int y_startE, const unsigned char *datasE,
                       unsigned int PART_COLUMN, unsigned int PART_LINE);
//Fast refresh display
void EPD_HW_Init_Fast(void);
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas);
void EPD_SetRAMValue_BaseMap_Fast(const unsigned char *datas);
//GUI display
void EPD_HW_Init_GUI(void);
void EPD_Display(unsigned char *Image);
//Update
void EPD_Update_Fast(void);
void EPD_Update(void);


void setup() {
  Serial.begin(115200);
  //epd
  pinMode(4, INPUT);    // BUSY
  pinMode(16, OUTPUT);  // RESET
  pinMode(17, OUTPUT);  // DC
  pinMode(5, OUTPUT);   // CS
  pinMode(23, OUTPUT);  //SDA
  pinMode(18, OUTPUT);  //SCL
  //touch
  pinMode(36, INPUT);   // Touch INT (VP)
  //front light enable
  pinMode(19, OUTPUT);
  digitalWrite(19, HIGH);  // Try to enable front light
}
//Tips//
/*When the electronic paper is refreshed in full screen, the picture flicker is a normal phenomenon, and the main function is to clear the display afterimage in the previous picture.
  When the local refresh is performed, the screen does not flash.*/
/*When you need to transplant the driver, you only need to change the corresponding IO. The BUSY pin is the input mode and the others are the output mode. */
int IRQ_Pin = 36;
#define Is_INT_IN digitalRead(IRQ_Pin)
int EpdNum;
void loop() {

  unsigned char i;
  Serial.print("System is run OK\r\n");
  FT6336_Init();  //Touch init

  //Clear screen
  EPD_HW_Init();            //Full screen refresh initialization.
  EPD_WhiteScreen_White();  //Clear screen function.
  delay(1000);
  EPD_HW_Init_Fast();                                     //Fast refresh initialization.
  EPD_SetRAMValue_BaseMap_Fast(gImage_basemapT);          //To display one image using fast refresh
  EPD_Dis_Part_Time(360, 124 + 48 * 0, Num[0],            //Y-A,X-A,DATA-A
                    360, 124 + 48 * 1, Num[0],            //Y-B,X-B,DATA-B
                    360, 124 + 48 * 2, Num[0],            //Y-C,X-C,DATA-C
                    360, 124 + 48 * 3, Num[0],            //Y-D,X-D,DATA-D
                    360, 124 + 48 * 4, Num[0], 48, 104);  //Y-E,X-E,DATA-E,Resolution

  while (1) {
    if (Is_INT_IN == 0)  //Touch is OK
    {
      TPR_Structure.TouchSta |= TP_COORD_UD;
      if (TPR_Structure.TouchSta & TP_COORD_UD) {
        TPR_Structure.TouchSta = 0;
        Serial.print("Is_INT_IN==0\r\n");
        FT6336_Scan();

        Serial.print(touch_count);
        Serial.print("\r\n");
        switch (touch_count) {
          case 1:
            if ((TPR_Structure.x[0] != 0) && (TPR_Structure.y[0] != 0)
                && (TPR_Structure.x[0] > 240) && (TPR_Structure.x[0] < 480)
                && (TPR_Structure.y[0] > 400) && (TPR_Structure.y[0] < 800)) {
              EpdNum++;
              if (EpdNum == 10)
                EpdNum = 0;
              Serial.println(EpdNum);
              EPD_Dis_Part_Time(360, 124 + 48 * 0, Num[0],
                                360, 124 + 48 * 1, Num[0],
                                360, 124 + 48 * 2, Num[0],
                                360, 124 + 48 * 3, Num[0],
                                360, 124 + 48 * 4, Num[EpdNum], 48, 104);
            } else if ((TPR_Structure.x[0] != 0) && (TPR_Structure.y[0] != 0)
                     && (TPR_Structure.x[0] > 0) && (TPR_Structure.x[0] <= 240)
                     && (TPR_Structure.y[0] > 400) && (TPR_Structure.y[0] < 800)) {
              if (EpdNum != 0)
                EpdNum--;
              if (EpdNum == 0)
                EpdNum = 9;
              Serial.println(EpdNum);
              EPD_Dis_Part_Time(360, 124 + 48 * 0, Num[0],
                                360, 124 + 48 * 1, Num[0],
                                360, 124 + 48 * 2, Num[0],
                                360, 124 + 48 * 3, Num[0],
                                360, 124 + 48 * 4, Num[EpdNum], 48, 104);
            }
            Serial.print(TPR_Structure.x[0]);
            Serial.print(",");
            Serial.println(TPR_Structure.y[0]);
            break;
          case 2:
            if ((TPR_Structure.x[0] != 0) && (TPR_Structure.y[0] != 0)
                && (TPR_Structure.x[1] != 0) && (TPR_Structure.y[1] != 0)) {
              Serial.println(touch_count);
              Serial.print(TPR_Structure.x[0]);
              Serial.print(",");
              Serial.println(TPR_Structure.y[0]);
              Serial.print(TPR_Structure.x[1]);
              Serial.print(",");
              Serial.println(TPR_Structure.y[1]);
            }
            break;
          default:
            break;
        }
        for (i = 0; i < 2; i++) {
          TPR_Structure.x[i] = 0;
          TPR_Structure.y[i] = 0;
        }
      }
    }
  }
}




///////////////////EXTERNAL FUNCTION////////////////////////////////////////////////////////////////////////
//////////////////////SPI///////////////////////////////////
void GPIO_IO(unsigned char i) {
  if (i == 0) {
    pinMode(23, INPUT);  //SDA
  } else {
    pinMode(23, OUTPUT);  //SDA
  }
}
void SPI_Delay(unsigned char xrate) {
  unsigned char i;
  while (xrate) {
    for (i = 0; i < 2; i++)
      ;
    xrate--;
  }
}
void SPI_Write(unsigned char value) {
  unsigned char i;
  SPI_Delay(1);
  for (i = 0; i < 8; i++) {
    EPD_W21_CLK_0;
    SPI_Delay(1);
    if (value & 0x80)
      EPD_W21_MOSI_1;
    else
      EPD_W21_MOSI_0;
    value = (value << 1);
    SPI_Delay(1);
    EPD_W21_CLK_1;
    SPI_Delay(1);
  }
}
//SPI write command
void EPD_W21_WriteCMD(unsigned char command) {
  EPD_W21_CS_0;
  EPD_W21_DC_0;  // D/C#   0:command  1:data
  SPI_Write(command);
  EPD_W21_CS_1;
}
//SPI write data
void EPD_W21_WriteDATA(unsigned char datas) {
  EPD_W21_CS_0;
  EPD_W21_DC_1;  // D/C#   0:command  1:data
  SPI_Write(datas);
  EPD_W21_CS_1;
}
unsigned char EPD_W21_ReadDATA(void) {

  unsigned char i, j = 0;
  GPIO_IO(0);
  EPD_W21_CS_0;
  EPD_W21_DC_1;  // command write(Must be added)
  EPD_W21_MOSI_1;
  SPI_Delay(2);
  for (i = 0; i < 8; i++) {
    EPD_W21_CLK_0;
    SPI_Delay(20);
    j = (j << 1);
    if (EPD_W21_READ == 1)
      j |= 0x01;
    else
      j &= ~0x01;
    SPI_Delay(20);
    EPD_W21_CLK_1;
    SPI_Delay(5);
  }
  EPD_W21_CS_1;
  GPIO_IO(1);
  return (j);
}


/////////////////EPD settings Functions/////////////////////



////////////////////////////////////E-paper demo//////////////////////////////////////////////////////////
//Busy function
void Epaper_READBUSY(void) {
  while (1) {  //=1 BUSY
    if (isEPD_W21_BUSY == 0) break;
  }
}
//Full screen refresh initialization
void EPD_HW_Init(void) {
  EPD_W21_RST_0;  // Module reset
  delay(10);      //At least 10ms delay
  EPD_W21_RST_1;
  delay(10);  //At least 10ms delay

  Epaper_READBUSY();
  EPD_W21_WriteCMD(0x12);  //SWRESET
  Epaper_READBUSY();

  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x0C);
  EPD_W21_WriteDATA(0xAE);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteDATA(0xC3);
  EPD_W21_WriteDATA(0xC0);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x01);  //Driver output control
  EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
  EPD_W21_WriteDATA(0x02);

  EPD_W21_WriteCMD(0x3C);  //BorderWavefrom
  EPD_W21_WriteDATA(0x01);

  EPD_W21_WriteCMD(0x11);  //data entry mode
  EPD_W21_WriteDATA(0x03);

  EPD_W21_WriteCMD(0x44);  //set Ram-X address start/end position
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
  EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);

  EPD_W21_WriteCMD(0x45);  //set Ram-Y address start/end position
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);


  EPD_W21_WriteCMD(0x4E);  // set RAM x address count to 0;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteCMD(0x4F);  // set RAM y address count to 0X199;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  Epaper_READBUSY();
}
//Fast refresh 1 initialization
void EPD_HW_Init_Fast(void) {
  EPD_W21_RST_0;  // Module reset
  delay(10);      //At least 10ms delay
  EPD_W21_RST_1;
  delay(10);  //At least 10ms delay

  Epaper_READBUSY();
  EPD_W21_WriteCMD(0x12);  //SWRESET
  Epaper_READBUSY();

  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x0C);
  EPD_W21_WriteDATA(0xAE);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteDATA(0xC3);
  EPD_W21_WriteDATA(0xC0);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x01);  //Driver output control
  EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
  EPD_W21_WriteDATA(0x02);

  EPD_W21_WriteCMD(0x3C);  //BorderWavefrom
  EPD_W21_WriteDATA(0x01);

  EPD_W21_WriteCMD(0x11);  //data entry mode
  EPD_W21_WriteDATA(0x03);

  EPD_W21_WriteCMD(0x44);  //set Ram-X address start/end position
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
  EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);

  EPD_W21_WriteCMD(0x45);  //set Ram-Y address start/end position
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);


  EPD_W21_WriteCMD(0x4E);  // set RAM x address count to 0;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteCMD(0x4F);  // set RAM y address count to 0X199;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  Epaper_READBUSY();

  //TEMP (1.5s)
  EPD_W21_WriteCMD(0x1A);
  EPD_W21_WriteDATA(0x5A);

  EPD_W21_WriteCMD(0x22);
  EPD_W21_WriteDATA(0x91);
  EPD_W21_WriteCMD(0x20);

  Epaper_READBUSY();
}


//////////////////////////////Display Update Function///////////////////////////////////////////////////////
//Partial refresh update function
void EPD_Part_Update(void) {
  EPD_W21_WriteCMD(0x22);  //Display Update Control
  EPD_W21_WriteDATA(0xFF);
  EPD_W21_WriteCMD(0x20);  //Activate Display Update Sequence
  Epaper_READBUSY();
}
//////////////////////////////Display Data Transfer Function////////////////////////////////////////////
//Full screen refresh display function
void EPD_WhiteScreen_ALL(const unsigned char *datas) {
  unsigned int i;
  EPD_W21_WriteCMD(0x24);  //write RAM for black(0)/white (1)
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_Update();
}
//Fast refresh display function
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas) {
  unsigned int i;
  EPD_W21_WriteCMD(0x24);  //write RAM for black(0)/white (1)
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_Update_Fast();
}

//Clear screen display
void EPD_WhiteScreen_White(void) {
  unsigned int i;
  EPD_W21_WriteCMD(0x24);  //write RAM for black(0)/white (1)
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(0xff);
  }
  EPD_Update();
}
//Display all black
void EPD_WhiteScreen_Black(void) {
  unsigned int i;
  EPD_W21_WriteCMD(0x24);  //write RAM for black(0)/white (1)
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(0x00);
  }
  EPD_Update();
}
//Partial refresh of background display, this function is necessary, please do not delete it!!!
void EPD_SetRAMValue_BaseMap(const unsigned char *datas) {
  unsigned int i;
  EPD_W21_WriteCMD(0x24);  //Write Black and White image to RAM
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_W21_WriteCMD(0x26);  //Write Black and White image to RAM
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_Update();
}
void EPD_SetRAMValue_BaseMap_Fast(const unsigned char *datas) {
  unsigned int i;
  EPD_W21_WriteCMD(0x24);  //Write Black and White image to RAM
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_W21_WriteCMD(0x26);  //Write Black and White image to RAM
  for (i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_Update_Fast();
}
//Partial refresh display
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start, const unsigned char *datas, unsigned int PART_COLUMN, unsigned int PART_LINE) {
  unsigned int i;
  unsigned int x_end, y_end;

  x_start = x_start - x_start % 8;    //x address start
  x_end = x_start + PART_LINE - 1;    //x address end
  y_start = y_start;                  //Y address start
  y_end = y_start + PART_COLUMN - 1;  //Y address end

  //Reset
  EPD_W21_RST_0;  // Module reset
  delay(10);      //At least 10ms delay
  EPD_W21_RST_1;
  delay(10);  //At least 10ms delay

  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x3C);  //BorderWavefrom
  EPD_W21_WriteDATA(0x80);
  //

  EPD_W21_WriteCMD(0x44);            // set RAM x address start/end
  EPD_W21_WriteDATA(x_start % 256);  //x address start2
  EPD_W21_WriteDATA(x_start / 256);  //x address start1
  EPD_W21_WriteDATA(x_end % 256);    //x address end2
  EPD_W21_WriteDATA(x_end / 256);    //x address end1
  EPD_W21_WriteCMD(0x45);            // set RAM y address start/end
  EPD_W21_WriteDATA(y_start % 256);  //y address start2
  EPD_W21_WriteDATA(y_start / 256);  //y address start1
  EPD_W21_WriteDATA(y_end % 256);    //y address end2
  EPD_W21_WriteDATA(y_end / 256);    //y address end1

  EPD_W21_WriteCMD(0x4E);            // set RAM x address count to 0;
  EPD_W21_WriteDATA(x_start % 256);  //x address start2
  EPD_W21_WriteDATA(x_start / 256);  //x address start1
  EPD_W21_WriteCMD(0x4F);            // set RAM y address count to 0X127;
  EPD_W21_WriteDATA(y_start % 256);  //y address start2
  EPD_W21_WriteDATA(y_start / 256);  //y address start1


  EPD_W21_WriteCMD(0x24);  //Write Black and White image to RAM
  for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_Part_Update();
}
//Full screen partial refresh display
void EPD_Dis_PartAll(const unsigned char *datas) {
  unsigned int i;
  unsigned int PART_COLUMN, PART_LINE;
  PART_COLUMN = EPD_HEIGHT, PART_LINE = EPD_WIDTH;

  //Reset
  EPD_W21_RST_0;  // Module reset
  delay(10);      //At least 10ms delay
  EPD_W21_RST_1;
  delay(10);  //At least 10ms delay

  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x3C);  //BorderWavefrom
  EPD_W21_WriteDATA(0x80);
  //

  EPD_W21_WriteCMD(0x24);  //Write Black and White image to RAM
  for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
  EPD_Part_Update();
}
//Deep sleep function
void EPD_DeepSleep(void) {
  EPD_W21_WriteCMD(0x10);  //Enter deep sleep
  EPD_W21_WriteDATA(0x01);
  delay(100);
}

//Partial refresh write address and data
void EPD_Dis_Part_RAM(unsigned int x_start, unsigned int y_start, const unsigned char *datas, unsigned int PART_COLUMN, unsigned int PART_LINE) {
  unsigned int i;
  unsigned int x_end, y_end;

  x_start = x_start - x_start % 8;    //x address start
  x_end = x_start + PART_LINE - 1;    //x address end
  y_start = y_start;                  //Y address start
  y_end = y_start + PART_COLUMN - 1;  //Y address end

  //Reset
  EPD_W21_RST_0;  // Module reset
  delay(10);      //At least 10ms delay
  EPD_W21_RST_1;
  delay(10);  //At least 10ms delay

  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x3C);  //BorderWavefrom
  EPD_W21_WriteDATA(0x80);
  //

  EPD_W21_WriteCMD(0x44);            // set RAM x address start/end
  EPD_W21_WriteDATA(x_start % 256);  //x address start2
  EPD_W21_WriteDATA(x_start / 256);  //x address start1
  EPD_W21_WriteDATA(x_end % 256);    //x address end2
  EPD_W21_WriteDATA(x_end / 256);    //x address end1
  EPD_W21_WriteCMD(0x45);            // set RAM y address start/end
  EPD_W21_WriteDATA(y_start % 256);  //y address start2
  EPD_W21_WriteDATA(y_start / 256);  //y address start1
  EPD_W21_WriteDATA(y_end % 256);    //y address end2
  EPD_W21_WriteDATA(y_end / 256);    //y address end1

  EPD_W21_WriteCMD(0x4E);            // set RAM x address count to 0;
  EPD_W21_WriteDATA(x_start % 256);  //x address start2
  EPD_W21_WriteDATA(x_start / 256);  //x address start1
  EPD_W21_WriteCMD(0x4F);            // set RAM y address count to 0X127;
  EPD_W21_WriteDATA(y_start % 256);  //y address start2
  EPD_W21_WriteDATA(y_start / 256);  //y address start1

  EPD_W21_WriteCMD(0x24);  //Write Black and White image to RAM
  for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }
}
//Clock display
void EPD_Dis_Part_Time(unsigned int x_startA, unsigned int y_startA, const unsigned char *datasA,
                       unsigned int x_startB, unsigned int y_startB, const unsigned char *datasB,
                       unsigned int x_startC, unsigned int y_startC, const unsigned char *datasC,
                       unsigned int x_startD, unsigned int y_startD, const unsigned char *datasD,
                       unsigned int x_startE, unsigned int y_startE, const unsigned char *datasE,
                       unsigned int PART_COLUMN, unsigned int PART_LINE) {
  EPD_Dis_Part_RAM(x_startA, y_startA, datasA, PART_COLUMN, PART_LINE);
  EPD_Dis_Part_RAM(x_startB, y_startB, datasB, PART_COLUMN, PART_LINE);
  EPD_Dis_Part_RAM(x_startC, y_startC, datasC, PART_COLUMN, PART_LINE);
  EPD_Dis_Part_RAM(x_startD, y_startD, datasD, PART_COLUMN, PART_LINE);
  EPD_Dis_Part_RAM(x_startE, y_startE, datasE, PART_COLUMN, PART_LINE);
  EPD_Part_Update();
}




////////////////////////////////Other newly added functions////////////////////////////////////////////
//Display rotation 180 degrees initialization
void EPD_HW_Init_180(void) {
  EPD_W21_RST_0;  // Module reset
  delay(10);      //At least 10ms delay
  EPD_W21_RST_1;
  delay(10);  //At least 10ms delay

  Epaper_READBUSY();
  EPD_W21_WriteCMD(0x12);  //SWRESET
  Epaper_READBUSY();

  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x0C);
  EPD_W21_WriteDATA(0xAE);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteDATA(0xC3);
  EPD_W21_WriteDATA(0xC0);
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x01);  //Driver output control
  EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
  EPD_W21_WriteDATA(0x02);

  EPD_W21_WriteCMD(0x3C);  //BorderWavefrom
  EPD_W21_WriteDATA(0x01);

  EPD_W21_WriteCMD(0x11);   //data entry mode
  EPD_W21_WriteDATA(0x00);  //180

  EPD_W21_WriteCMD(0x44);  //set Ram-X address start/end position

  EPD_W21_WriteDATA((EPD_HEIGHT - 1) % 256);
  EPD_W21_WriteDATA((EPD_HEIGHT - 1) / 256);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);

  EPD_W21_WriteCMD(0x45);  //set Ram-Y address start/end position
  EPD_W21_WriteDATA((EPD_WIDTH - 1) % 256);
  EPD_W21_WriteDATA((EPD_WIDTH - 1) / 256);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);



  EPD_W21_WriteCMD(0x4E);  // set RAM x address count to 0;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteCMD(0x4F);  // set RAM y address count to 0X199;
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x00);
  Epaper_READBUSY();
}


/*******************************LUT******************************************/
//0--5
const unsigned char WS_0_5[112] = {
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x1E,	0x23,	0x21,	0x23,	0x00,						
0x28,	0x01,	0x28,	0x01,	0x03,						
0x1B,	0x19,	0x05,	0x03,	0x01,						
0x05,	0x00,	0x08,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
										
//5--10
const unsigned char WS_5_10[112] = {
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x1E,	0x23,	0x05,	0x02,	0x00,						
0x2B,	0x01,	0x2B,	0x01,	0x02,						
0x1B,	0x19,	0x05,	0x03,	0x00,						
0x05,	0x00,	0x07,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										

//10--15
const unsigned char WS_10_15[112] = {
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xAA,	0x48,	0x55,	0x44,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x55,	0x48,	0xAA,	0x88,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x14,	0x1A,	0x0B,	0x06,	0x00,						
0x21,	0x01,	0x21,	0x01,	0x02,						
0x18,	0x16,	0x05,	0x03,	0x00,						
0x04,	0x00,	0x05,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
										
//15---20
const unsigned char WS_15_20[112] = {
0xA2,	0x48,	0x51,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x48,	0xA8,	0x80,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA2,	0x48,	0x51,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x48,	0xA8,	0x80,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x0D,	0x0D,	0x08,	0x05,	0x00,						
0x0F,	0x01,	0x0F,	0x01,	0x04,						
0x0D,	0x0D,	0x05,	0x05,	0x00,						
0x03,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
		
//20----80
const unsigned char WS_20_80[112] = {
0xA0,	0x48,	0x54,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x50,	0x48,	0xA8,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA0,	0x48,	0x54,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x50,	0x48,	0xA8,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x1A,	0x14,	0x00,	0x00,	0x00,						
0x0D,	0x01,	0x0D,	0x01,	0x02,						
0x0A,	0x0A,	0x03,	0x00,	0x01,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x48,						
0x00,	0x00,									
};										
	
								
//80----127  Fast
const unsigned char WS_80_127[112] = {
0xA8,	0x00,	0x55,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x00,	0xAA,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0xA8,	0x00,	0x55,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x54,	0x00,	0xAA,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	
0x0C,	0x0D,	0x0B,	0x01,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x0A,	0x0A,	0x05,	0x0B,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x00,	0x00,						
0x00,	0x00,	0x00,	0x01,	0x01,						
0x22,	0x22,	0x22,	0x22,	0x22,						
0x17,	0x41,	0xA8,	0x32,	0x30,						
0x00,	0x00,									
};											

int Read_temp(void) {
  int tempvalue;
  unsigned char temp1, temp2;
  EPD_W21_WriteCMD(0x18);
  EPD_W21_WriteDATA(0X80);
  EPD_W21_WriteCMD(0x22);
  EPD_W21_WriteDATA(0XB1);
  EPD_W21_WriteCMD(0x20);
  Epaper_READBUSY();

  EPD_W21_WriteCMD(0x1B);
  temp1 = EPD_W21_ReadDATA();
  temp2 = EPD_W21_ReadDATA();

  tempvalue = temp1 << 8 | temp2;
  tempvalue = tempvalue >> 4;
  tempvalue = tempvalue / 16;
  return tempvalue;
}

void Write_LUT(const unsigned char *wavefrom) {
  unsigned char i;
  EPD_W21_WriteCMD(0x32);  // write VCOM register
  for (i = 0; i < 105; i++) {
    EPD_W21_WriteDATA(*wavefrom++);
  }
  Epaper_READBUSY();

  EPD_W21_WriteCMD(0x03);
  EPD_W21_WriteDATA(*wavefrom++);

  EPD_W21_WriteCMD(0x04);
  EPD_W21_WriteDATA(*wavefrom++);
  EPD_W21_WriteDATA(*wavefrom++);
  EPD_W21_WriteDATA(*wavefrom++);

  EPD_W21_WriteCMD(0x2C);  ///vcom
  EPD_W21_WriteDATA(*wavefrom++);
}


void Write_LUT_All(void) {
  float temp;
  temp = Read_temp();

  if (temp <= 5) {
    Write_LUT(WS_0_5);
  } else if (temp <= 10) {
    Write_LUT(WS_5_10);
  } else if (temp <= 15) {
    Write_LUT(WS_10_15);
  } else if (temp <= 20) {
    Write_LUT(WS_15_20);
  } else {
    Write_LUT(WS_20_80);
  }
}
void Write_LUT_Fast(void) {
  Write_LUT(WS_80_127);
}

void EPD_Update(void) {
  Write_LUT_All();

  EPD_W21_WriteCMD(0x22);
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteCMD(0x20);
  Epaper_READBUSY();
}
//Fast update function
void EPD_Update_Fast(void) {
  Write_LUT_Fast();

  Epaper_READBUSY();
  EPD_W21_WriteCMD(0x22);  //Display Update Control
  EPD_W21_WriteDATA(0xC7);
  EPD_W21_WriteCMD(0x20);  //Activate Display Update Sequence
  Epaper_READBUSY();
}
