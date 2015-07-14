#include "gpio.h"
#include "common.h"
#include "uart.h"
#include "dma.h"

#include "i2c.h"
#include "ov7725.h"

#include "oled_spi.h"

#include "ftm.h"
#include "lptmr.h"
#include "pit.h"


#define offset 77
void turn(float angel){
    angel += offset;
    int pwm = (int)((angel/90.0 + 0.5) * 500);  //90度是1.5ms
    //printf("turn:%d\r\n", pwm);
    FTM_PWM_ChangeDuty(HW_FTM1, HW_FTM_CH0, pwm);
}

#define DRIVER_PWM_WIDTH 1000
void setSpeed(int spd);
void initDriver(){
    printf("initPWM\r\n");

    //for(int i=0;i<0;i++)
    GPIO_QuickInit(HW_GPIOC, 0, kGPIO_Mode_OPP);
    PCout(0)=1;
    //使能INH

    FTM_PWM_QuickInit(FTM1_CH0_PA12, kPWM_EdgeAligned, 50);     //设置FTM，边沿对齐模式
    turn(0);
    //初始化舵机

    FTM_PWM_QuickInit(FTM0_CH0_PC01, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH1_PC02, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH2_PC03, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH3_PC04, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    //初始化电机PWM输出
    setSpeed(0);
}

void setLeftSpeed(int spd){
    if(spd>0){
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH1, spd);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH0, 0);
    }else
    {
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH1, 0);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH0, -spd);
    }
}

void setRightSpeed(int spd){
    if(spd>0){
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH2, spd);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, 0);
    }else
    {
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH2, 0);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, -spd);
    }
}

/* 请将I2C.H中的 I2C_GPIO_SIM 改为 1 */

// 改变图像大小
//0: 80x60
//1: 160x120
//2: 240x180
#define IMAGE_SIZE  1

#if (IMAGE_SIZE  ==  0)
#define OV7620_W    (80)
#define OV7620_H    (60)

#elif (IMAGE_SIZE == 1)
#define OV7620_W    (160)
#define OV7620_H    (120)

#elif (IMAGE_SIZE == 2)
#define OV7620_W    (240)
#define OV7620_H    (180)

#else
#error "Image Size Not Support!"
#endif

#define WIDTH OV7620_W
#define HEIGHT OV7620_H-1

// 图像内存池
uint8_t gCCD_RAM[(OV7620_H)*((OV7620_W/8)+1)];   //使用内部RAM

/* 行指针 */
uint8_t * gpHREF[OV7620_H+1];

/* 引脚定义 PCLK VSYNC HREF 接到同一个PORT上 */
#define BOARD_OV7620_PCLK_PORT      HW_GPIOE
#define BOARD_OV7620_PCLK_PIN       (8)
#define BOARD_OV7620_VSYNC_PORT     HW_GPIOE
#define BOARD_OV7620_VSYNC_PIN      (10)
#define BOARD_OV7620_HREF_PORT      HW_GPIOE
#define BOARD_OV7620_HREF_PIN       (9)
/*
摄像头数据引脚PTA8-PTA15 只能填入 0 8 16三个值
0 :PTA0-PTA7
8 :PTA8-PTA15
16:PTA16-PTA24
*/
#define BOARD_OV7620_DATA_OFFSET    (0)

static void UserApp(uint32_t vcount);
//定义一帧结束后的用户函数

/* 状态机定义 */
typedef enum
{
    TRANSFER_IN_PROCESS, //数据在处理
    NEXT_FRAME,          //下一帧数据
}OV7620_Status;

int SCCB_Init(uint32_t I2C_MAP)
{
    int r;
    uint32_t instance;
    instance = I2C_QuickInit(I2C_MAP, 50*1000);
    r = ov7725_probe(instance);
    if(r)
    {
        return 1;
    }
    r = ov7725_set_image_size((ov7725_size)IMAGE_SIZE);
    if(r)
    {
        printf("OV7725 set image error\r\n");
        return 1;
    }
    return 0;
}

//行中断和场中断都使用PTE中断
void OV_ISR(uint32_t index)
{
    static uint8_t status = TRANSFER_IN_PROCESS;
    static uint32_t h_counter, v_counter;
   // uint32_t i;

    /* 行中断 */
    if(index & (1 << BOARD_OV7620_HREF_PIN))
    {
        DMA_SetDestAddress(HW_DMA_CH2, (uint32_t)gpHREF[h_counter++]);
        //i = DMA_GetMajorLoopCount(HW_DMA_CH2);
        DMA_SetMajorLoopCounter(HW_DMA_CH2, (OV7620_W/8)+1);
        DMA_EnableRequest(HW_DMA_CH2);

        return;
    }
    /* 场中断 */
    if(index & (1 << BOARD_OV7620_VSYNC_PIN))
    {
        GPIO_ITDMAConfig(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_IT_FallingEdge, false);
        GPIO_ITDMAConfig(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_IT_FallingEdge, false);
        switch(status)
        {
            case TRANSFER_IN_PROCESS: //接受到一帧数据调用用户处理
                    UserApp(v_counter++);
                    //printf("i:%d %d\r\n", h_counter, i);
                    status = NEXT_FRAME;
                    h_counter = 0;

                break;
            case NEXT_FRAME: //等待下次传输
                status =  TRANSFER_IN_PROCESS;
                break;
            default:
                break;
        }
        GPIO_ITDMAConfig(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_IT_FallingEdge, true);
        GPIO_ITDMAConfig(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_IT_FallingEdge, true);
        PORTE->ISFR = 0xFFFFFFFF;   //这里可以改PORTE
        h_counter = 0;
        return;
    }
}

void initCamera(){
    uint32_t i;

    printf("OV7725 test\r\n");

    //检测摄像头
    if(SCCB_Init(I2C0_SCL_PB00_SDA_PB01))
    {
        printf("no ov7725device found!\r\n");
        while(1);
    }
    printf("OV7620 setup complete\r\n");

    //每行数据指针
    for(i=0; i<OV7620_H+1; i++)
    {
        gpHREF[i] = (uint8_t*)&gCCD_RAM[i*OV7620_W/8];
    }

    DMA_InitTypeDef DMA_InitStruct1 = {0};

    /* 场中断  行中断 像素中断 */
    GPIO_QuickInit(BOARD_OV7620_PCLK_PORT, BOARD_OV7620_PCLK_PIN, kGPIO_Mode_IPD);
    GPIO_QuickInit(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_Mode_IPD);
    GPIO_QuickInit(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_Mode_IPD);

    /* install callback */
    GPIO_CallbackInstall(BOARD_OV7620_VSYNC_PORT, OV_ISR);

    GPIO_ITDMAConfig(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_IT_FallingEdge, true);
    GPIO_ITDMAConfig(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_IT_FallingEdge, true);
    GPIO_ITDMAConfig(BOARD_OV7620_PCLK_PORT, BOARD_OV7620_PCLK_PIN, kGPIO_DMA_RisingEdge, true);

    /* 初始化数据端口 */
    for(i=0;i<8;i++)
    {
        GPIO_QuickInit(HW_GPIOE, BOARD_OV7620_DATA_OFFSET+i, kGPIO_Mode_IFT);
    }

    //DMA配置
    DMA_InitStruct1.chl = HW_DMA_CH2;
    DMA_InitStruct1.chlTriggerSource = PORTE_DMAREQ;    //这里可以改PORTE
    DMA_InitStruct1.triggerSourceMode = kDMA_TriggerSource_Normal;
    DMA_InitStruct1.minorLoopByteCnt = 1;
    DMA_InitStruct1.majorLoopCnt = ((OV7620_W/8) +1);

    DMA_InitStruct1.sAddr = (uint32_t)&PTE->PDIR + BOARD_OV7620_DATA_OFFSET/8;  //这里可以改PTE
    DMA_InitStruct1.sLastAddrAdj = 0;
    DMA_InitStruct1.sAddrOffset = 0;
    DMA_InitStruct1.sDataWidth = kDMA_DataWidthBit_8;
    DMA_InitStruct1.sMod = kDMA_ModuloDisable;

    DMA_InitStruct1.dAddr = (uint32_t)gpHREF[0];
    DMA_InitStruct1.dLastAddrAdj = 0;
    DMA_InitStruct1.dAddrOffset = 1;
    DMA_InitStruct1.dDataWidth = kDMA_DataWidthBit_8;
    DMA_InitStruct1.dMod = kDMA_ModuloDisable;

    /* initialize DMA moudle */
    DMA_Init(&DMA_InitStruct1);
}


bool printflag = false;
//串口接收中断
void UART_RX_ISR(uint16_t byteRec){
    //打印整个图像
    if(byteRec == ' ')printflag = true;

}
// IMG
uint8_t IMG[OV7620_W][OV7620_H];   //使用内部RAM


int white[HEIGHT];
bool crossflag = false;
bool rectflag = false;

void fixLine(int black, int y, bool isblack){
    //宽度大于5的黑条或白条
    if(white[y]>WIDTH-5)crossflag=true;else rectflag=false;

    int crossstart = y+black+15;
    int crossend = y-10;
    if(crossend<0)crossend=0;
    if(crossstart>HEIGHT)crossstart=WIDTH-1;
    if(white[crossstart]>10&&white[crossend]>10){

        //起始位置从中心向两边搜索边界
        int left1 = WIDTH/2;
        int right1 = WIDTH/2;
        while(left1){
            if(IMG[left1][crossstart] == 1 && (IMG[left1+1][crossstart] == 0))break;
            left1--;
        }
        while(right1 < WIDTH-1){
            if(IMG[right1][crossstart] == 0 && (IMG[right1+1][crossstart] == 1))break;
            right1++;
        }
        //printf("left1=%d,right1=%d\n", left1, right1);

        //终止位置从中心向两边搜索边界
        int left2;
        int right2;
        float k1 = 0;
        float k2 = 0;
        
        left2 = WIDTH/2;
        right2 = WIDTH/2;
        while(left2){
            if(IMG[left2][crossend] == 1 && (IMG[left2+1][crossend] == 0))break;
            left2--;
        }
        while(right2 < WIDTH-1){
            if(IMG[right2][crossend] == 0 && (IMG[right2+1][crossend] == 1))break;
            right2++;
        }
        IMG[left2][crossend] = 2;
        IMG[right2][crossend] = 2;

        k1 = (float)(left2-left1)/(crossend-crossstart);
        k2 = (float)(right2-right1)/(crossend-crossstart);
        //f(k1<0 && k2>0)break;

        //两边补线
        for(int i=0;i<(crossstart-crossend) && crossend+i < HEIGHT-1;i++){
            IMG[(int)(left2+k1*i)][crossend+i] = isblack?2:0;
            IMG[(int)(right2+k2*i)][crossend+i] = isblack?2:0;
        }
    }
}

void findType(){
    int x,y;
    for(y=0;y<HEIGHT;y++){
        int whitedots=0;
        for(x=0;x<WIDTH;x++){
            if(IMG[x][y]==0)whitedots++;
        }
        white[y] = whitedots;
    }
    //统计白点个数

    int black=0;bool lastblack=false;
    int whitesum=0;bool lastwhite=false;
    
    for(y=HEIGHT;y>0;y--){

        if(white[y]<3){
            //一条黑条
            if(lastblack)black++;   //上一次也是黑,则黑++
            else black=1;   //第一次黑
            lastblack=true;
        }else{
            lastblack=false;
            if(black>3){    //黑大于5次
                rectflag=true;  //直角标志
                fixLine(black, y, false);
            }else{
                rectflag = false;
            }
            black=0;
        }

        if(white[y]>WIDTH-3){
            //一条白
            if(lastwhite)whitesum++;   //上一次也是黑,则黑++
            else whitesum=1;   //第一次黑
            lastwhite=true;
        }else{
            lastwhite=true;
            if(whitesum>5){
                crossflag=true;
                fixLine(whitesum, y, true);
            }else{
                crossflag = false;
            }
            whitesum=0;
        }

    }

}
void findCenter();

#define DELTA_MAX 3
int average;

/* 接收完成一场后 用户处理函数 */
static void UserApp(uint32_t vcount)
{
    for(int y=0;y<OV7620_H-1;y++)
        for(int x=0;x<OV7620_W/8;x++)
            for(int i=0; i<8; i++)
                IMG[x*8+i][y] = (gpHREF[y][x+1]>>(7-i))%2;
    //将图片从OV7620_H*OV7620_W/8映射到OV7620_H*OV7620_W

    findType();
    findCenter();
    
    if(printflag){
        printflag = false;
        //打印出图像
        printf("start\r\n");
        for(int y=0;y<OV7620_H-1;y++){
            for(int x=0;x<OV7620_W;x++){
                printf("%d", IMG[x][y]);
            }
            printf("\r\n");
        }
        
    }
    
    
    //打印到屏幕上
    for(int y=0;y<8;y++){
        LED_WrCmd(0xb0 + y); //0xb0+0~7表示页0~7
        LED_WrCmd(0x00); //0x00+0~16表示将128列分成16组其地址在某组中的第几列
        LED_WrCmd(0x10); //0x10+0~16表示将128列分成16组其地址所在第几组
        for(int x=0;x<80;x++){
            uint8_t data = 0;
            for(int i=0;i<8 && (y*8+i)*2<HEIGHT ;i++){
                data += (IMG[x*2][(y*8+i)*2] > 0 | IMG[x*2+1][(y*8+i)*2] > 0)<<(i);
            }
            LED_WrDat(data);
        }
    }
    
    if(crossflag)LED_P8x16Str(80, 0, "cross ");
    else LED_P8x16Str(80, 0, "normal");

    
}

int centers[HEIGHT] = {0};

int err2 = 0;
void findCenter(){
    int y=HEIGHT;

    int center = WIDTH/2;       //赛道中心
    int s = 0;          //中心累积求和
    int sum = 0;        //中心数
    
    int left;
    int right;
    
    int err = 0;
    
    while(y){
        left = center-1;
        right = center+1;
        
        while(left){
            if((IMG[left][y]==1)&&(IMG[left+1][y]==0))break;
            left--;
        }
        while(right < WIDTH){
            if((IMG[right-1][y]==0)&&(IMG[right][y]==1))break;
            right++;
        }
        
        center = (left+right)/2;
        centers[sum] = center;
        if(right-left<10){
            err++;
            break;
        }
        if(err>12)break;
        s += center;
        sum ++;
        IMG[center][y] = 1;
        y--;
    }
    
    char buf[20]={0};
    
    if(sum>10){
        average = s/sum;
        average -= 80;
        LED_P8x16Str(80, 3, buf);
        turn(average/1.5);
        setSpeed(1400+(sum-60)*20+(25-abs(average))*20);
        err2=0;
    }else{
        err2++;
        if(err2>200){
            setSpeed(0);
            //turn(0);
            //while(1);
        }
    }

    sprintf(buf, "a=%d ", average);
    LED_P8x16Str(80, 1, buf);
    sprintf(buf, "h=%d ", sum);
    LED_P8x16Str(80, 2, buf);
}

float differ = 0;

void setSpeed(int spd){
#define kchasu 60
    if(average>0){
        setLeftSpeed(spd+spd*average/kchasu);
        setRightSpeed(spd);
    }else{
        setLeftSpeed(spd);
        setRightSpeed(spd-spd*average/kchasu);
    }
}

int speed = 0;
float PSpeed = 0.1;
void PIT_ISR(void)
{
    int value; /* 记录正交脉冲个数 */
    uint8_t dir; /* 记录编码器旋转方向1 */
    /* 获取正交解码数据 */
    FTM_QD_GetData(HW_FTM2, &value, &dir);
    if(!dir)value=65535-value;
    
    //printf("value:%6d dir:%d  \r\n", value, dir);
    
    FTM_QD_ClearCount(HW_FTM2); /* 如测量频率则需要定时清除Count值  */
}

int main(void)
{
    DelayInit();
    /* 打印串口及小灯 */
    
    GPIO_QuickInit(HW_GPIOD, 10, kGPIO_Mode_OPP);
    UART_QuickInit(UART0_RX_PB16_TX_PB17, 115200);
    /* 注册中断回调函数 */
    UART_CallbackRxInstall(HW_UART0, UART_RX_ISR);

    /* 开启UART Rx中断 */
    UART_ITDMAConfig(HW_UART0, kUART_IT_Rx, true);

    initOLED();
    initDriver();
    initCamera();
    
    //用户控制部分
    GPIO_QuickInit(HW_GPIOC, 8, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 10, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 12, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 14, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 16, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 18, kGPIO_Mode_IPU);
#define differadd 0.02
    if(PCin(8))differ += differadd;
    if(PCin(10))differ += differadd;
    if(PCin(12))differ += differadd;
    if(PCin(14))differ += differadd;
    if(PCin(16))differ += differadd;
    if(PCin(18))differ += differadd;
    
    printf("differ=%f\r\n", differ);
    
    //编码器初始化
    FTM_QD_QuickInit(FTM2_QD_PHA_PA10_PHB_PA11, kFTM_QD_NormalPolarity, kQD_PHABEncoding);
    
    /* 开启PIT中断 */
    PIT_QuickInit(HW_PIT_CH0, 1000*100);
    PIT_CallbackInstall(HW_PIT_CH0, PIT_ISR);
    PIT_ITDMAConfig(HW_PIT_CH0, kPIT_IT_TOF, true);
    
    GPIO_QuickInit(HW_GPIOB, 3, kGPIO_Mode_IPU);
    
    int test = 0;
    int last = 0;
    while(1)
    {
        DelayMs(100);
//        if(PBin(3)){
//            if(test>0)test--;
//        }
//        else {
//            test = 50;
//        }
//        PDout(10) = test>0;
//        if((test>0)!=last){
//            printf("change,%d\r\n", test>0);
//            last = test>0;
//        }
    }
}
