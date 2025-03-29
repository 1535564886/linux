#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdalign.h>
#include <linux/videodev2.h>

#define WIDTH 1920
#define HEIGHT 1080

int main(int argc, char const *argv[])
{
    //1打开设备
    int fd = open("/dev/video11", O_RDWR);
    if (fd < 0)
    {
        perror("打开设备失败");
    }
    printf("打开设备成功\n");

    //2获取摄像头支持的格式
    int ret;

    //3设置摄像头格式
    struct v4l2_format v4l2_fmt_set;
    v4l2_fmt_set.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_fmt_set.fmt.pix_mp.height = HEIGHT;
    v4l2_fmt_set.fmt.pix_mp.width = WIDTH;
    v4l2_fmt_set.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    v4l2_fmt_set.fmt.pix_mp.num_planes = 2;//NV12占用两个平面
    v4l2_fmt_set.fmt.pix_mp.plane_fmt[0].sizeimage = WIDTH * HEIGHT;//为两个平面设置大小，NV12格式下第二平面为第一平面到一半
    v4l2_fmt_set.fmt.pix_mp.plane_fmt[1].sizeimage = WIDTH * HEIGHT / 2;
    ret = ioctl(fd,VIDIOC_S_FMT,&v4l2_fmt_set);
    if(ret < 0)
    {
        perror("设置失败");
    }

    /*struct v4l2_format v4l2_fmt_get;
    v4l2_fmt_get.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ret = ioctl(fd,VIDIOC_G_FMT,&v4l2_fmt_get);
    if(ret < 0)
    {
        perror("查询格式失败");
    }
    char p[4];
    p[0] = v4l2_fmt_get.fmt.pix_mp.pixelformat & 0xff;
    p[1] = (v4l2_fmt_get.fmt.pix_mp.pixelformat >> 8) & 0xff;
    p[2] = (v4l2_fmt_get.fmt.pix_mp.pixelformat >> 16) & 0xff;
    p[3] = (v4l2_fmt_get.fmt.pix_mp.pixelformat >> 24) & 0xff;
    printf("width:%d\nheight:%d\npixelformat:%c%c%c%c\n",v4l2_fmt_get.fmt.pix_mp.width,v4l2_fmt_get.fmt.pix_mp.height,p[0],p[1],p[2],p[3]);
*/
    //4申请缓冲区队列,内核空间
    struct v4l2_requestbuffers req_buf;
    memset(&req_buf,0,sizeof(req_buf));
    req_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req_buf.count = 4;      //申请四个缓冲区
    req_buf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(fd,VIDIOC_REQBUFS,&req_buf);
    if(ret < 0)
    {
        perror("申请队列空间失败");
    }
    else
        printf("申请队列成功\n");
    printf("实际申请到的缓冲区数量：%d\n",req_buf.count);//查看实际申请到的缓冲区数量
    //5内存映射
    unsigned char *mpaddr[4];   //有四个缓冲区，设置一个指针数组，存放映射到用户区的四个数组首地址
    int buf_length[4];
    struct v4l2_buffer mapbuffer;
    //初始化type.index
    mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    mapbuffer.memory = V4L2_MEMORY_MMAP;
    for (int i = 0; i < req_buf.count; i++)
    {
        mapbuffer.index = i;
        mapbuffer.length = 2;//平面数量
        mapbuffer.m.planes = malloc(2*sizeof(struct v4l2_plane));//申请两个平面结构体大小的内存
        if(!mapbuffer.m.planes)
            perror("planes failure");
        ret = ioctl(fd,VIDIOC_QUERYBUF,&mapbuffer);//申请内核空间长度
        if(ret < 0)
        {
            perror("申请内核空间失败");
        }
        
        printf("申请到的实际长度:%d\n",mapbuffer.m.planes->length);
        
        //进行映射
        buf_length[i] = mapbuffer.m.planes->length;//存储长度用于后续释放映射，否则长度不一样会导致失败
        //按照申请到的实际长度进行映射
        mpaddr[i] = mmap(NULL , mapbuffer.m.planes->length , PROT_READ | PROT_WRITE , MAP_SHARED , fd , mapbuffer.m.planes->m.mem_offset);
        if(mpaddr[i] == MAP_FAILED)
        {
            perror("映射失败");
        }

        ret = ioctl(fd,VIDIOC_QBUF,&mapbuffer);
        if(ret < 0)
            perror("查询后入队失败");
        //释放内存
        free(mapbuffer.m.planes);
    }
    
    //6开始采集
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ret = ioctl(fd,VIDIOC_STREAMON,&type);
    if(ret < 0)
        perror("开启采集失败");
    //从队列中读取一帧数据
    struct v4l2_buffer read = {0};
    read.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    read.memory = V4L2_MEMORY_MMAP;
    read.length = 2;//平面长度
    read.m.planes = malloc(2*sizeof(struct v4l2_plane));//申请两个平面的内存
    ret = ioctl(fd,VIDIOC_DQBUF,&read);
    if(ret < 0)
        perror("读取数据失败");


    FILE *file = fopen("my.nv12","wb");
    size_t plane_size = read.m.planes->length;//往文件中写入一帧数据，即保存为图片
    fwrite(mpaddr[read.index],1,plane_size,file);//读取会告诉占用了哪个缓冲区，从而寻找对应地址，写入数量是1,大小是是平面大小
    fclose(file);
    free(read.m.planes);

    //通知内核使用完成，开始入队，只需要入队使用过到缓冲区
    struct v4l2_buffer qbuf = {0};
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = read.index;
    qbuf.length = 2;
    qbuf.m.planes = malloc(2*sizeof(struct v4l2_plane));

    if(ioctl(fd,VIDIOC_QBUF,&qbuf) < 0)
    {
        perror("qbuf failure");
    }
    free(qbuf.m.planes);

    //停止采集
    ret = ioctl(fd,VIDIOC_STREAMOFF,&type);
    if(ret < 0)
        perror("停止采集失败");
    //7释放映射
    for (int i = 0; i < 4; i++)
    {

        if(mpaddr[i] != MAP_FAILED)
        ret = munmap(mpaddr[i],buf_length[i]);
        if(ret < 0)
            perror("释放内存映射失败");
    }
    //9关闭设备
    close(fd);

    return 0;
}
