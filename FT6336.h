#ifndef FT6336_H
#define FT6336_H	 
#include "i2c.h"
#include <arduino.h>



void find_ic_addr(void);
void i2c_write_byte_check(unsigned char txd);
unsigned char FT6336_read_firmware_id(void);
unsigned char FT6336_read_device_mode(void);
unsigned char FT6336_read_td_status(void);
unsigned int FT6336_read_touch1_x(void);
unsigned int FT6336_read_touch1_y(void);
unsigned char FT6336_read_touch1_event(void);
unsigned char FT6336_read_touch1_id(void);

unsigned char FT6336_read_touch2_event(void);
unsigned char FT6336_read_touch2_id(void);
unsigned int FT6336_read_touch2_x(void);
unsigned int FT6336_read_touch2_y(void);
void FT6336_Scan(void);
extern unsigned char touch_count;//����������־λ


#define TP_PRES_DOWN 0x80  //����������	
#define TP_COORD_UD  0x40  //����������±��

//������������ݽṹ�嶨��
typedef struct			
{
	unsigned char TouchSta;	//���������b7:����1/�ɿ�0; b6:0û�а�������/1�а�������;bit5:������bit4-bit0�����㰴����Ч��־����ЧΪ1���ֱ��Ӧ������5-1��
	unsigned int x[5];		//֧��5�㴥������Ҫʹ��5������洢����������
	unsigned int y[5];
	
}TouchPointRefTypeDef;
extern TouchPointRefTypeDef TPR_Structure;

#define I2C_ADDR_FT6336 0x38

//FT6236 ���ּĴ������� 
#define FT_DEVIDE_MODE 			0x00   		//FT6236ģʽ���ƼĴ���
#define FT_REG_NUM_FINGER       0x02		//����״̬�Ĵ���

#define FT_TP1_REG 				0X03	  	//��һ�����������ݵ�ַ
#define FT_TP2_REG 				0X09		//�ڶ������������ݵ�ַ
#define FT_TP3_REG 				0X0F		//���������������ݵ�ַ
#define FT_TP4_REG 				0X15		//���ĸ����������ݵ�ַ
#define FT_TP5_REG 				0X1B		//��������������ݵ�ַ  
 

#define	FT_ID_G_LIB_VERSION		0xA1		//�汾		
#define FT_ID_G_MODE 			0xA4   		//FT6236�ж�ģʽ���ƼĴ���
#define FT_ID_G_THGROUP			0x80   		//������Чֵ���üĴ���
#define FT_ID_G_PERIODACTIVE	0x88   		//����״̬�������üĴ���  
#define Chip_Vendor_ID          0xA3        //оƬID(0x36)
#define ID_G_FT6236ID			0xA8		//0x11


#define FT6336_ADDR_DEVICE_MODE 	0x00
#define FT6336_ADDR_TD_STATUS 		0x02
#define FT6336_ADDR_TOUCH1_EVENT 	0x03
#define FT6336_ADDR_TOUCH1_ID 		0x05
#define FT6336_ADDR_TOUCH1_X 		0x03
#define FT6336_ADDR_TOUCH1_Y 		0x05

#define FT6336_ADDR_TOUCH2_EVENT 	0x09
#define FT6336_ADDR_TOUCH2_ID 		0x0B
#define FT6336_ADDR_TOUCH2_X 		0x09
#define FT6336_ADDR_TOUCH2_Y 		0x0B

#define FT6336_ADDR_FIRMARE_ID 		0xA6


#endif
