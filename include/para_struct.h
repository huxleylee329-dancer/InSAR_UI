/*-------------------------------------------------------------------*/
/*          参数结构体文件，用于模板处理时传参给处理进程             */
/*-------------------------------------------------------------------*/
#pragma once
#define MAX_FILE_LENGTH 1024
/**
* DEM处理模板参数结构体
*/
struct template_DEM_para
{
	/*工程名*/
	char project_name[MAX_FILE_LENGTH];
	/*工作目录*/
	char working_directory[MAX_FILE_LENGTH];
	/*主图像文件*/
	char master_image_file[MAX_FILE_LENGTH];
	/*辅图像文件*/
	char slave_image_file[MAX_FILE_LENGTH];


	/*-----------------*/
	/*   图像导入参数  */
	/*-----------------*/

	/*传感器名*/
	char sensor[256];


	/*-----------------*/
	/*   AOI裁剪参数   */
	/*-----------------*/

	/*AOI中心经度*/
	double AOI_center_lon;
	/*AOI中心纬度*/
	double AOI_center_lat;
	/*AOI高度(m)*/
	double AOI_height;
	/*AOI宽度(m)*/
	double AOI_width;


	/*-----------------*/
	/*  图像配准参数   */
	/*-----------------*/

	/*插值倍数*/
	int coregis_interp_times;
	/*子块大小*/
	int coregis_blocksize;

	/*---------------------*/
	/*  干涉相位生成参数   */
	/*---------------------*/

	/*距离向多视倍数*/
	int ifg_multilook_rg;
	/*方位向多视倍数*/
	int ifg_multilook_az;
	/*是否估计相关系数*/
	bool ifg_b_coh;
	/*相关系数估计方位向窗口大小*/
	int ifg_coh_wndsize_az;
	/*相关系数估计距离向窗口大小*/
	int ifg_coh_wndsize_rg;

	/*-----------------*/
	/*  相位滤波参数   */
	/*-----------------*/

	/*滤波方法(1：斜坡自适应，2：Goldstein，3：深度学习)*/
	int denoise_method;
	/*斜坡自适应预滤波窗口大小*/
	int slope_pre_size;
	/*斜坡自适应滤波窗口大小*/
	int slope_size;
	/*Goldstein滤波程度（0-1之间）*/
	double Goldstein_alpha;
	/*Goldstein滤波FFT窗口大小*/
	int Goldstein_FFT_wdnsize;
	/*Goldstein滤波FFT补零窗口大小*/
	int Goldstein_pad;

	/*-----------------*/
	/*  相位解缠参数   */
	/*-----------------*/

	/*解缠方法（1：质量图法，2：最小费用流法，3：SNAPHU，4：综合解缠法）*/
	int unwrap_method;
	/*综合解缠法相关系数阈值(0-1之间)*/
	double unwrap_coh_thresh;

	/*-----------------*/
    /*  高程反演参数   */
    /*-----------------*/

	/*牛顿迭代法迭代次数*/
	int iter_times;


	template_DEM_para()
	{
		project_name[0] = 0;
		working_directory[0] = 0;
		master_image_file[0] = 0;
		slave_image_file[0] = 0;
		sensor[0] = 0;

		AOI_center_lat = 0.0;
		AOI_center_lon = 0.0;
		AOI_width = 5000;
		AOI_height = 5000;

		coregis_interp_times = 8;
		coregis_blocksize = 256;

		ifg_b_coh = true;
		ifg_coh_wndsize_az = 3;
		ifg_coh_wndsize_rg = 3;
		ifg_multilook_az = 1;
		ifg_multilook_rg = 1;

		denoise_method = 2;
		slope_pre_size = 11;
		slope_size = 11;
		Goldstein_alpha = 0.9;
		Goldstein_FFT_wdnsize = 64;
		Goldstein_pad = 8;

		unwrap_method = 2;
		unwrap_coh_thresh = 0.7;

		iter_times = 20;
	}

};

/*DEM模板处理子进程回传文件参数结构体（用于更新界面工程树和xml工程文件）*/
struct template_DEM_para_back
{
	/*--------------*/
	/*   导入数据   */
	/*--------------*/

	/*导入数据节点名*/
	char import_node_name[MAX_FILE_LENGTH];
	/*导入主图像文件名（不带路径和.h5后缀）*/
	char import_master_name[MAX_FILE_LENGTH];
	/*导入辅图像文件名（不带路径和.h5后缀）*/
	char import_slave_name[MAX_FILE_LENGTH];
	/*导入主图像文件*/
	char import_master_file[MAX_FILE_LENGTH];
	/*导入辅图像文件*/
	char import_slave_file[MAX_FILE_LENGTH];

	/*--------------*/
	/*   裁剪AOI    */
	/*--------------*/

	/*裁剪AOI数据节点名*/
	char AOI_node_name[MAX_FILE_LENGTH];
	/*裁剪主图像文件名（不带路径和.h5后缀）*/
	char AOI_master_name[MAX_FILE_LENGTH];
	/*裁剪辅图像文件名（不带路径和.h5后缀）*/
	char AOI_slave_name[MAX_FILE_LENGTH];
	/*裁剪主图像文件*/
	char AOI_master_file[MAX_FILE_LENGTH];
	/*裁剪辅图像文件*/
	char AOI_slave_file[MAX_FILE_LENGTH];


	/*--------------*/
	/*     配准     */
	/*--------------*/

	/*配准数据节点名*/
	char regis_node_name[MAX_FILE_LENGTH];
	/*配准主图像文件名（不带路径和.h5后缀）*/
	char regis_master_name[MAX_FILE_LENGTH];
	/*配准辅图像文件名（不带路径和.h5后缀）*/
	char regis_slave_name[MAX_FILE_LENGTH];
	/*配准主图像文件*/
	char regis_master_file[MAX_FILE_LENGTH];
	/*配准辅图像文件*/
	char regis_slave_file[MAX_FILE_LENGTH];


	/*--------------*/
	/*     干涉     */
	/*--------------*/

	/*干涉相位生成数据节点名*/
	char ifg_node_name[MAX_FILE_LENGTH];
	/*干涉相位文件名（不带路径和.h5后缀）*/
	char ifg_name[MAX_FILE_LENGTH];
	/*干涉相位文件*/
	char ifg_file[MAX_FILE_LENGTH];
	/*相关系数文件名（不带路径和.h5后缀）*/
	char ifg_coherence_name[MAX_FILE_LENGTH];

	/*--------------*/
	/*     滤波     */
	/*--------------*/

	/*滤波数据节点名*/
	char denoise_node_name[MAX_FILE_LENGTH];
	/*滤波相位文件名（不带路径和.h5后缀）*/
	char denoise_ifg_name[MAX_FILE_LENGTH];
	/*滤波相位文件*/
	char denoise_ifg_file[MAX_FILE_LENGTH];

	/*--------------*/
	/*     解缠     */
	/*--------------*/

	/*解缠数据节点名*/
	char unwrap_node_name[MAX_FILE_LENGTH];
	/*解缠相位文件名（不带路径和.h5后缀）*/
	char unwrap_ifg_name[MAX_FILE_LENGTH];
	/*解缠相位文件*/
	char unwrap_ifg_file[MAX_FILE_LENGTH];


	/*--------------*/
	/*     高程     */
	/*--------------*/

	/*高程反演数据节点名*/
	char dem_node_name[MAX_FILE_LENGTH];
	/*高程文件名（不带路径和.h5后缀）*/
	char dem_name[MAX_FILE_LENGTH];
	/*高程文件*/
	char dem_file[MAX_FILE_LENGTH];


	template_DEM_para_back()
	{
		import_node_name[0] = 0; import_master_name[0] = 0; import_master_file[0] = 0; import_slave_name[0] = 0; import_slave_file[0] = 0;
		AOI_node_name[0] = 0; AOI_master_name[0] = 0; AOI_master_file[0] = 0; AOI_slave_name[0] = 0; AOI_slave_file[0] = 0;
		regis_node_name[0] = 0; regis_master_name[0] = 0; regis_master_file[0] = 0; regis_slave_name[0] = 0; regis_slave_file[0] = 0;
		ifg_node_name[0] = 0; ifg_name[0] = 0; ifg_file[0] = 0; ifg_coherence_name[0] = 0;
		denoise_node_name[0] = 0; denoise_ifg_name[0] = 0; denoise_ifg_file[0] = 0;
		unwrap_node_name[0] = 0; unwrap_ifg_name[0] = 0; unwrap_ifg_file[0] = 0;
		dem_node_name[0] = 0; dem_name[0] = 0; dem_file[0] = 0;
	}
};