# RK3588/RK3576 YOLO11 DEMO

在rk3588/rk3576上使用yolo11进行目标识别

## 快速开始

1. 环境

```bash
sudo apt update
sudo apt install cmake
sudo apt install gcc g++
sudo apt install -y libopencv-dev pkg-config
sudo apt install -y \
libgstreamer1.0-dev \
libgstreamer-plugins-base1.0-dev \
libgtk-3-dev \
pkg-config
sudo apt install -y libgstrtspserver-1.0-dev
sudo apt-get install -y librga-dev
sudo apt-get install qtbase5-dev
```

2. 编写配置文件`config.json`，见`config.json.example`
3. 运行脚本`build_linux_rk.sh`

4. 效果，左方为USB摄像头输入，右方为RTSP输入。图像分辨率均为1920x1080，模型Yolov11s

![image-20260301200819681](./assets/2026-03-02_125748.png)

## 特性

1. 兼容UVC、MIPI、RTSP、视频四种输入
2. UVC采用jpeg_turbo解码，RTSP使用gstreamer、mpp硬解
3. RGA预处理
4. 模型权重复用
5. 每路可自定义多线程处理
6. NPU推理，轮询分配NPU核心
7. CPU后处理
8. 多路显示墙

## Todo

1. rk3576下RGA调用可能有越界写，目前需要开辟更大的缓冲区防止报错？
2. 非零拷贝
3. qt5重写显示UI
4. 单路单线程优化

## 参考

[rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo)和[EASY-EAI](https://github.com/EASY-EAI/EASY-EAI-Toolkit-3576)

## 其它

- RTSP模拟：[EasyRTSPServer
](https://github.com/EasyDarwin/EasyRTSPServer)
- [performance.sh](./performance.sh)是给RK3588的，不要在RK3576上运行
