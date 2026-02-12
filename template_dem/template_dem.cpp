#include"para_struct.h"
#include"InSAR_IPC.h"
#include"ComplexMat.h"
#include"Utils.h"
#include"Registration.h"
#include"Deflat.h"
#include"Filter.h"
#include"Unwrap.h"
#include"Dem.h"
#include"FormatConversion.h"

#include<direct.h>
#include<tchar.h>
#include<atlstr.h>
#include<atlconv.h>
#include<string>
#include <iostream>
#include<time.h>

#ifdef DEBUG
#pragma comment(lib,"ComplexMat_d.lib")
#pragma comment(lib,"Utils_d.lib")
#pragma comment(lib,"Registration_d.lib")
#pragma comment(lib,"Deflat_d.lib")
#pragma comment(lib,"Filter_d.lib")
#pragma comment(lib,"Unwrap_d.lib")
#pragma comment(lib,"Dem_d.lib")
#pragma comment(lib,"FormatConversion_d.lib")
#else
#pragma comment(lib,"ComplexMat.lib")
#pragma comment(lib,"Utils.lib")
#pragma comment(lib,"Registration.lib")
#pragma comment(lib,"Deflat.lib")
#pragma comment(lib,"Filter.lib")
#pragma comment(lib,"Unwrap.lib")
#pragma comment(lib,"Dem.lib")
#pragma comment(lib,"FormatConversion.lib")
#endif // DEBUG

#define MAX_MSG_LENGTH 1024
using cv::Mat;
/** @brief 进程间通信回调函数
* @param IPC                    进程通信类指针
* @param msgType                消息类型
* @param progress               进度信息
* @param msg                    消息内容
* @param callback_para          回传参数
*/
int processCallback(InSAR_IPC* IPC, int msgType, int progress, const char* msg, template_DEM_para_back* callback_para = NULL)
{
	USES_CONVERSION;

	if (IPC == NULL || msg == NULL)
	{
		return -1;
	}
	stcIPCMsgHeader msgHeader;
	
	strcpy(msgHeader.message, msg);
	msgHeader.progress = progress;
	if (msgType == eCallbackType_UpdateCmplt)
	{
		msgHeader.msgType = eCallbackType_UpdateCmplt;
	}
	else if (msgType == eCallbackType_MsgFinished)
	{
		msgHeader.msgType = eCallbackType_MsgFinished;
		if (callback_para)
		{
			memcpy((void*)&msgHeader.callback_para, (void*)callback_para, sizeof(template_DEM_para_back));
		}
	}
	else if (msgType == eCallbackType_MsgError)
	{
		msgHeader.msgType = eCallbackType_MsgError;
	}
	else
	{
		msgHeader.msgType = eCallbackType_Unknown;
	}
	DWORD dwOffset = 0;
	IPC->WriteData(dwOffset, (char*)&msgHeader, sizeof(stcIPCMsgHeader));
	// 等待主进程读取数据结束
	// 设置自己为无信号
	IPC->ResetEventIntf(true);
	// 设置主进程为有信号
	IPC->SetEventIntf(false);

	// 等待主进程将自己设置为有信号（如果发生错误或者完成任务则不再等待）
	if (msgType == eCallbackType_MsgError || msgType == eCallbackType_MsgFinished)
	{
		return 0;
	}
	DWORD dw = WaitForSingleObject(IPC->GetEvent(true), INFINITE);
	return 0;
}

/**
* 传入参数
* 0 可执行程序名
* 1 共享内存名
* 2 主进程事件名
* 3 子进程事件名
*/
int _tmain(int argc, _TCHAR* argv[])
{
	if (argc != 4) return -1;
	USES_CONVERSION;
	//获取可执行程序路径
	TCHAR chDllPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, chDllPath, MAX_PATH);
	CString strPath(chDllPath);
	CString strExe = "\\template_dem.exe";
	CString strExePath = strPath.TrimRight(strExe);
	string exe_path = W2A(strExePath);
	//初始化进程间通信
	InSAR_IPC* IPC = new InSAR_IPC();
	if (!IPC) return -1;
	bool b_init = IPC->InitIPCMemory(false, argv[1]);
	if (!b_init) return -1;
	b_init = IPC->OpenSelfEvent(argv[3]);
	if (!b_init) return -1;
	b_init = IPC->OpenOtherEvent(argv[2]);
	if (!b_init) return -1;


	//从共享内存读取参数

	DWORD dwOffset = 0;
	template_DEM_para para;
	if (!IPC->ReadData(dwOffset, sizeof(template_DEM_para), (char*)&para))
	{
		processCallback(IPC, eCallbackType_MsgError, 1, "Failed to read parameter from shared memory!");
		return -1;
	}


	processCallback(IPC, eCallbackType_UpdateCmplt, 1, "parameter get!");

	//开始处理
	FormatConversion conversion; Utils util; Registration regis; Deflat deflat; Filter filter; Unwrap unwrap; Dem dem;
	int ret; 
	int start;
	string working_directory(para.working_directory);
	std::replace(working_directory.begin(), working_directory.end(), '/', '\\');
	string w(para.project_name);
	working_directory = working_directory + "\\" +  w;
	//创建目录
	if (_mkdir(working_directory.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create project directory!");
		return -1;
	}
	string sensor(para.sensor);
	string master_file(para.master_image_file);
	std::replace(master_file.begin(), master_file.end(), '/', '\\');
	string slave_file(para.slave_image_file);
	std::replace(slave_file.begin(), slave_file.end(), '/', '\\');
	string import_dir = working_directory + "\\import";
	string AOI_dir = working_directory + "\\AOI";
	string coregistration_dir = working_directory + "\\coregistration";
	string ifg_dir = working_directory + "\\ifg";
	string denoise_dir = working_directory + "\\denoise";
	string unwrap_dir = working_directory + "\\unwrap";
	string dem_dir = working_directory + "\\dem";

	string dst_h5_master, dst_h5_slave;//导入文件
	string AOI_h5_master, AOI_h5_slave;//裁剪文件
	string tmp_master, tmp_slave;//导入文件名（不含路径和.h5后缀）
	string regis_h5_master, regis_h5_slave;//配准文件
	string ifg_coherence_h5, ifg_h5;//干涉相位生成文件
	string ifg_denoised_h5;//滤波文件
	string ifg_unwrapped_h5;//解缠文件
	string dem_h5;//高程文件
	vector<string> SAR_images;//配准输入
	vector<string> SAR_images_out;//配准输出

	/*-----------------------------------------------------------------------------*/
	/*                                导入数据                                     */
	/*-----------------------------------------------------------------------------*/

	//TerraSAR-X/TanDEM-X数据导入
	if (sensor == "TSX")
	{
		//创建目录
		if (_mkdir(import_dir.c_str()) < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "failed to create import directory!");
			return -1;
		}
		//从master_file中提取日期
		size_t r_pos = master_file.rfind("\\");
		if (r_pos < master_file.length() && r_pos >= 0)
		{
			tmp_master = master_file.substr(r_pos + 1, master_file.length() - 1 - r_pos);
			r_pos = tmp_master.rfind("T");
			if (r_pos < tmp_master.length() && r_pos >= 0)
			{
				start = tmp_master.rfind("T") - 8;
				start = start < 0 ? 0 : start;
				tmp_master = tmp_master.substr(start, 8);
			}
			else tmp_master = "master";
		}
		else tmp_master = "master";
		//从slave_file中提取日期
		r_pos = slave_file.rfind("\\");
		if (r_pos < slave_file.length() && r_pos >= 0)
		{
			tmp_slave = slave_file.substr(r_pos + 1, slave_file.length() - 1 - r_pos);
			r_pos = tmp_slave.rfind("T");
			if (r_pos < tmp_slave.length() && r_pos >= 0)
			{
				start = tmp_slave.rfind("T") - 8;
				start = start < 0 ? 0 : start;
				tmp_slave = tmp_slave.substr(start, 8);
			}
			else tmp_slave = "slave";
		}
		else tmp_slave = "slave";
		dst_h5_master = import_dir + "\\" + tmp_master + ".h5";
		dst_h5_slave = import_dir + "\\" + tmp_slave + ".h5";
		ret = conversion.TSX2h5(master_file.c_str(), dst_h5_master.c_str());
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "TSX2h5(): failed to import TSX!");
			return -1;
		}
		processCallback(IPC, eCallbackType_UpdateCmplt, 10, "master file imported!");
		ret = conversion.TSX2h5(slave_file.c_str(), dst_h5_slave.c_str());
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "TSX2h5(): failed to import TSX!");
			return -1;
		}
		processCallback(IPC, eCallbackType_UpdateCmplt, 20, "slave file imported!");



	}
	else if (sensor == "sentinel")
	{
		processCallback(IPC, eCallbackType_MsgError, 1, "sentinel not supported yet!");
		return -1;
	}
	else
	{
		processCallback(IPC, eCallbackType_MsgError, 1, "unknown sensor!");
		return -1;
	}

	/*-----------------------------------------------------------------------------*/
	/*                                裁剪AOI                                      */
	/*-----------------------------------------------------------------------------*/
	
	//检查参数
	if (para.AOI_center_lat < -90.0 ||
		para.AOI_center_lat > 90.0 ||
		para.AOI_center_lon < -180.0 ||
		para.AOI_center_lon > 180.0
		)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "invalid AOI center coordinates!");
		return -1;
	}
	para.AOI_height = para.AOI_height < 2000 ? 2000 : para.AOI_height;//高度不少于2km
	para.AOI_width = para.AOI_width < 2000 ? 2000 : para.AOI_width;//宽度不少于2km

	//创建裁剪AOI目录
	if (_mkdir(AOI_dir.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create AOI directory!");
		return -1;
	}
	
	AOI_h5_master = AOI_dir + "\\" + tmp_master + "_cut.h5";
	AOI_h5_slave = AOI_dir + "\\" + tmp_slave + "_cut.h5";
	ComplexMat SLC;
	int offset_row_master = 0;
	int offset_col_master = 0;
	ret = util.get_AOI_from_h5slc(dst_h5_master.c_str(),
		para.AOI_center_lon,//lon
		para.AOI_center_lat,//lat
		para.AOI_width,//width
		para.AOI_height,//height
		SLC, &offset_row_master, &offset_col_master
	);//裁剪主图
	if (ret < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "get_AOI_from_h5slc(): failed to get master AOI!");
		return -1;
	}
	int AOI_azimuth_len = SLC.GetRows();
	int AOI_range_len = SLC.GetCols();
	ret = conversion.creat_new_h5(AOI_h5_master.c_str());
	ret = conversion.write_slc_to_h5(AOI_h5_master.c_str(), SLC);
	ret = conversion.Copy_para_from_h5_2_h5(dst_h5_master.c_str(), AOI_h5_master.c_str());//拷贝其他参数
	ret = conversion.write_str_to_h5(AOI_h5_master.c_str(), "process_state", "cut");
	ret = conversion.write_str_to_h5(AOI_h5_master.c_str(), "comment", "complex-1.0");
	Mat tmp = Mat::zeros(1, 1, CV_32SC1);
	tmp.at<int>(0, 0) = SLC.GetRows();
	ret = conversion.write_array_to_h5(AOI_h5_master.c_str(), "azimuth_len", tmp);
	tmp.at<int>(0, 0) = SLC.GetCols();
	ret = conversion.write_array_to_h5(AOI_h5_master.c_str(), "range_len", tmp);
	tmp.at<int>(0, 0) = offset_row_master;
	ret = conversion.write_array_to_h5(AOI_h5_master.c_str(), "offset_row", tmp);
	tmp.at<int>(0, 0) = offset_col_master;
	ret = conversion.write_array_to_h5(AOI_h5_master.c_str(), "offset_col", tmp);
	processCallback(IPC, eCallbackType_UpdateCmplt, 30, "master image AOI get!");

	int offset_row_slave = 0;
	int offset_col_slave = 0;
	ret = util.get_AOI_from_h5slc(dst_h5_slave.c_str(),
		para.AOI_center_lon,//lon
		para.AOI_center_lat,//lat
		para.AOI_width,//width
		para.AOI_height,//height
		SLC, &offset_row_slave, &offset_col_slave
	);//裁剪辅图
	if (ret < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "get_AOI_from_h5slc(): failed to get slave AOI!");
		return -1;
	}
	ret = conversion.creat_new_h5(AOI_h5_slave.c_str());
	ret = conversion.write_slc_to_h5(AOI_h5_slave.c_str(), SLC);
	ret = conversion.Copy_para_from_h5_2_h5(dst_h5_slave.c_str(), AOI_h5_slave.c_str());//拷贝其他参数
	ret = conversion.write_str_to_h5(AOI_h5_slave.c_str(), "process_state", "cut");
	ret = conversion.write_str_to_h5(AOI_h5_slave.c_str(), "comment", "complex-1.0");
	tmp.at<int>(0, 0) = SLC.GetRows();
	ret = conversion.write_array_to_h5(AOI_h5_slave.c_str(), "azimuth_len", tmp);
	tmp.at<int>(0, 0) = SLC.GetCols();
	ret = conversion.write_array_to_h5(AOI_h5_slave.c_str(), "range_len", tmp);
	tmp.at<int>(0, 0) = offset_row_slave;
	ret = conversion.write_array_to_h5(AOI_h5_slave.c_str(), "offset_row", tmp);
	tmp.at<int>(0, 0) = offset_col_slave;
	ret = conversion.write_array_to_h5(AOI_h5_slave.c_str(), "offset_col", tmp);
	processCallback(IPC, eCallbackType_UpdateCmplt, 40, "slave image AOI get!");

	/*-----------------------------------------------------------------------------*/
	/*                                图像配准                                     */
	/*-----------------------------------------------------------------------------*/

	//创建目录
	if (_mkdir(coregistration_dir.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create coregistration directory!");
		return -1;
	}

	regis_h5_master = coregistration_dir + "\\" + tmp_master + "_cut_regis.h5";
	regis_h5_slave = coregistration_dir + "\\" + tmp_slave + "_cut_regis.h5";

	SAR_images.push_back(AOI_h5_master);
	SAR_images.push_back(AOI_h5_slave);
	SAR_images_out.push_back(regis_h5_master);
	SAR_images_out.push_back(regis_h5_slave);

	//检查参数
	para.coregis_interp_times = para.coregis_interp_times < 2 ? 2 : para.coregis_interp_times;
	if (para.coregis_blocksize % 2 != 0) para.coregis_blocksize = para.coregis_blocksize + 1;
	para.coregis_blocksize = para.coregis_blocksize < 64 ? 64 : para.coregis_blocksize;

	ret = util.stack_coregistration(SAR_images, SAR_images_out, 1, para.coregis_interp_times, para.coregis_blocksize);
	if (ret < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "stack_coregistration(): failed in coregistration!");
		return -1;
	}
	ret = conversion.Copy_para_from_h5_2_h5(SAR_images[0].c_str(), SAR_images_out[0].c_str());
	ret = conversion.write_str_to_h5(SAR_images_out[0].c_str(), "process_state", "coregistration");
	ret = conversion.write_str_to_h5(SAR_images_out[0].c_str(), "comment", "complex-2.0");
	tmp.at<int>(0, 0) = AOI_azimuth_len;
	ret = conversion.write_array_to_h5(SAR_images_out[0].c_str(), "azimuth_len", tmp);
	tmp.at<int>(0, 0) = AOI_range_len;
	ret = conversion.write_array_to_h5(SAR_images_out[0].c_str(), "range_len", tmp);

	ret = conversion.Copy_para_from_h5_2_h5(SAR_images[1].c_str(), SAR_images_out[1].c_str());
	ret = conversion.write_str_to_h5(SAR_images_out[1].c_str(), "process_state", "coregistration");
	ret = conversion.write_str_to_h5(SAR_images_out[1].c_str(), "comment", "complex-2.0");
	tmp.at<int>(0, 0) = AOI_azimuth_len;
	ret = conversion.write_array_to_h5(SAR_images_out[1].c_str(), "azimuth_len", tmp);
	tmp.at<int>(0, 0) = AOI_range_len;
	ret = conversion.write_array_to_h5(SAR_images_out[1].c_str(), "range_len", tmp);

	processCallback(IPC, eCallbackType_UpdateCmplt, 60, "image coregistration finished!");


	/*-----------------------------------------------------------------------------*/
	/*                                干涉相位生成                                 */
	/*-----------------------------------------------------------------------------*/

	//创建目录
	if (_mkdir(ifg_dir.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create interferometric formayion directory!");
		return -1;
	}
	ifg_h5 = ifg_dir + "\\" + tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase.h5";

	//检查参数
	para.ifg_coh_wndsize_az = (para.ifg_coh_wndsize_az % 2 == 1) ? para.ifg_coh_wndsize_az : para.ifg_coh_wndsize_az + 1;
	para.ifg_coh_wndsize_rg = (para.ifg_coh_wndsize_rg % 2 == 1) ? para.ifg_coh_wndsize_rg : para.ifg_coh_wndsize_rg + 1;
	para.ifg_coh_wndsize_az = para.ifg_coh_wndsize_az < 3 ? 3 : para.ifg_coh_wndsize_az;
	para.ifg_coh_wndsize_az = para.ifg_coh_wndsize_az > 99 ? 99 : para.ifg_coh_wndsize_az;
	para.ifg_coh_wndsize_rg = para.ifg_coh_wndsize_rg < 3 ? 3 : para.ifg_coh_wndsize_rg;
	para.ifg_coh_wndsize_rg = para.ifg_coh_wndsize_rg > 99 ? 99 : para.ifg_coh_wndsize_rg;

	para.ifg_multilook_az = para.ifg_multilook_az < 1 ? 1 : para.ifg_multilook_az;
	para.ifg_multilook_rg = para.ifg_multilook_rg < 1 ? 1 : para.ifg_multilook_rg;

	para.ifg_multilook_az = para.ifg_multilook_az > 100 ? 100 : para.ifg_multilook_az;
	para.ifg_multilook_rg = para.ifg_multilook_rg > 100 ? 100 : para.ifg_multilook_rg;

	SLC.release();


	ComplexMat Master, Slave;
	Mat phase;
	ret = conversion.read_slc_from_h5(regis_h5_slave.c_str(), Slave);
	ret = conversion.read_slc_from_h5(regis_h5_master.c_str(), Master);
	if (Master.type() != CV_64F) Master.convertTo(Master, CV_64F);
	if (Slave.type() != CV_64F) Slave.convertTo(Slave, CV_64F);
	ret = util.multilook(Master, Slave, para.ifg_multilook_rg, para.ifg_multilook_az, phase);
	if (ret < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "multilook(): failed to generate ifg!");
		return -1;
	}
	ret = conversion.creat_new_h5(ifg_h5.c_str());
	string master_relative_path = "\\coregistration\\" + tmp_master + "_cut_regis.h5";
	string slave_relative_path = "\\coregistration\\" + tmp_slave + "_cut_regis.h5";
	std::replace(slave_relative_path.begin(), slave_relative_path.end(), '\\', '/');
	std::replace(master_relative_path.begin(), master_relative_path.end(), '\\', '/');
	ret = conversion.write_str_to_h5(ifg_h5.c_str(), "source_1", master_relative_path.c_str());
	ret = conversion.write_str_to_h5(ifg_h5.c_str(), "source_2", slave_relative_path.c_str());
	Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
	tmp_int.at<int>(0, 0) = phase.rows;
	ret = conversion.write_array_to_h5(ifg_h5.c_str(), "azimuth_len", tmp_int);
	tmp_int.at<int>(0, 0) = phase.cols;
	ret = conversion.write_array_to_h5(ifg_h5.c_str(), "range_len", tmp_int);

	//去平地
	Mat slave_statevec, master_statevec, lon_coef, lat_coef, flat_phase_coefficient, phase_deflatted;
	double slave_interval, master_interval, lambda;
	ret = conversion.read_array_from_h5(regis_h5_slave.c_str(), "state_vec", slave_statevec);
	ret = conversion.read_array_from_h5(regis_h5_master.c_str(), "state_vec", master_statevec);
	ret = conversion.read_array_from_h5(regis_h5_master.c_str(), "lon_coefficient", lon_coef);
	ret = conversion.read_array_from_h5(regis_h5_master.c_str(), "lat_coefficient", lat_coef);
	ret = conversion.read_array_from_h5(regis_h5_slave.c_str(), "prf", tmp);
	slave_interval = 1 / tmp.at<double>(0, 0);
	ret = conversion.read_array_from_h5(regis_h5_master.c_str(), "prf", tmp);
	master_interval = 1 / tmp.at<double>(0, 0);
	ret = conversion.read_array_from_h5(regis_h5_master.c_str(), "carrier_frequency", tmp);
	lambda = 3e8 / tmp.at<double>(0, 0);
	ret = deflat.deflat(master_statevec, slave_statevec, lon_coef, lat_coef, phase, offset_row_master, offset_col_master, 0,
		master_interval, slave_interval, 1, lambda, phase_deflatted, flat_phase_coefficient);
	if (ret < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "deflat(): failed to deflat ifg!");
		return -1;
	}
	ret = conversion.write_array_to_h5(ifg_h5.c_str(), "phase", phase_deflatted);
	ret = conversion.write_array_to_h5(ifg_h5.c_str(), "flat_phase_coefficient", flat_phase_coefficient);
	if (para.ifg_b_coh)
	{
		ret = util.phase_coherence(phase_deflatted, para.ifg_coh_wndsize_rg, para.ifg_coh_wndsize_az, phase);
		ret = conversion.write_array_to_h5(ifg_h5.c_str(), "coherence", phase);
	}
	processCallback(IPC, eCallbackType_UpdateCmplt, 65, "ifg generation finished!");


	/*-----------------------------------------------------------------------------*/
	/*                                干涉相位滤波                                 */
	/*-----------------------------------------------------------------------------*/

	//创建目录
	if (_mkdir(denoise_dir.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create denoise directory!");
		return -1;
	}

	ifg_denoised_h5 = denoise_dir + "\\" + tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase_denoised.h5";

	//检查参数并滤波

	if (para.denoise_method == 1)//斜坡自适应
	{
		para.slope_pre_size = para.slope_pre_size % 2 == 1 ? para.slope_pre_size : para.slope_pre_size + 1;
		para.slope_size = para.slope_size % 2 == 1 ? para.slope_size : para.slope_size + 1;
		para.slope_pre_size = para.slope_pre_size < 3 ? 3 : para.slope_pre_size;
		para.slope_size = para.slope_size < 3 ? 3 : para.slope_size;
		ret = filter.slope_adaptive_filter(phase_deflatted, phase, para.slope_size, para.slope_pre_size);
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "slope_adaptive_filter(): failed to denoise ifg!");
			return -1;
		}
	}
	else if (para.denoise_method == 3)//深度学习滤波
	{
		string dl_model_file = exe_path + "\\other\\net.pt";
		ret = filter.filter_dl(exe_path.c_str(), denoise_dir.c_str(), dl_model_file.c_str(), phase_deflatted, phase);
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "filter_dl(): failed to denoise ifg!");
			return -1;
		}
	}
	else//Goldstein滤波
	{
		para.Goldstein_alpha = para.Goldstein_alpha < 0.0 ? 0.0 : para.Goldstein_alpha;
		para.Goldstein_alpha = para.Goldstein_alpha > 1.0 ? 1.0 : para.Goldstein_alpha;
		para.Goldstein_FFT_wdnsize = para.Goldstein_FFT_wdnsize < 8 ? 8 : para.Goldstein_FFT_wdnsize;
		para.Goldstein_pad = para.Goldstein_pad < 4 ? 4 : para.Goldstein_pad;
		ret = filter.Goldstein_filter(phase_deflatted, phase, para.Goldstein_alpha, para.Goldstein_FFT_wdnsize, para.Goldstein_pad);
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "Goldstein_filter(): failed to denoise ifg!");
			return -1;
		}
	}

	ret = conversion.creat_new_h5(ifg_denoised_h5.c_str());
	ret = conversion.write_array_to_h5(ifg_denoised_h5.c_str(), "phase", phase);
	string tmp_str;
	ret = conversion.read_str_from_h5(ifg_h5.c_str(), "source_1", tmp_str);
	ret = conversion.write_str_to_h5(ifg_denoised_h5.c_str(), "source_1", tmp_str.c_str());
	ret = conversion.read_str_from_h5(ifg_h5.c_str(), "source_2", tmp_str);
	ret = conversion.write_str_to_h5(ifg_denoised_h5.c_str(), "source_2", tmp_str.c_str());
	if(!conversion.read_array_from_h5(ifg_h5.c_str(), "flat_phase_coefficient", tmp))
		conversion.write_array_to_h5(ifg_denoised_h5.c_str(), "flat_phase_coefficient", tmp);
	ret = conversion.read_array_from_h5(ifg_h5.c_str(), "range_len", tmp);
	ret = conversion.write_array_to_h5(ifg_denoised_h5.c_str(), "range_len", tmp);
	ret = conversion.read_array_from_h5(ifg_h5.c_str(), "azimuth_len", tmp);
	ret = conversion.write_array_to_h5(ifg_denoised_h5.c_str(), "azimuth_len", tmp);

	processCallback(IPC, eCallbackType_UpdateCmplt, 70, "ifg denoise finished!");

	/*-----------------------------------------------------------------------------*/
	/*                                干涉相位解缠                                 */
	/*-----------------------------------------------------------------------------*/

	//创建目录
	if (_mkdir(unwrap_dir.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create unwrap directory!");
		return -1;
	}
	ifg_unwrapped_h5 = unwrap_dir + "\\" + tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase_denoised_unwrapped.h5";

	if (para.unwrap_method == 1)//质量图
	{
		ret = unwrap.SPD_Guided_Unwrap(phase, phase_deflatted);
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "SPD_Guided_Unwrap(): failed to unwrap ifg!");
			return -1;
		}
	}
	else if (para.unwrap_method == 2)//最小费用流
	{
		string mcf_problem = unwrap_dir + "\\mcf_problem.net";
		Mat coherence, residue;
		ret = util.phase_coherence(phase, coherence);
		ret = util.residue(phase, residue);
		ret = unwrap.MCF(phase, phase_deflatted, coherence, residue, mcf_problem.c_str(), exe_path.c_str());
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "MCF(): failed to unwrap ifg!");
			return -1;
		}
	}
	else if (para.unwrap_method == 3)//SNAPHU
	{
		ret = unwrap.snaphu(ifg_denoised_h5.c_str(), phase_deflatted, working_directory.c_str(), unwrap_dir.c_str(), exe_path.c_str());
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "snaphu(): failed to unwrap ifg!");
			return -1;
		}
	}
	else//综合法
	{
		ret = unwrap.QualityGuided_MCF(phase, phase_deflatted, para.unwrap_coh_thresh, 5.0, unwrap_dir.c_str(), exe_path.c_str());
		if (ret < 0)
		{
			processCallback(IPC, eCallbackType_MsgError, 0, "QualityGuided_MCF(): failed to unwrap ifg!");
			return -1;
		}
	}

	ret = conversion.creat_new_h5(ifg_unwrapped_h5.c_str());
	ret = conversion.write_array_to_h5(ifg_unwrapped_h5.c_str(), "phase", phase_deflatted);
	ret = conversion.read_str_from_h5(ifg_denoised_h5.c_str(), "source_1", tmp_str);
	ret = conversion.write_str_to_h5(ifg_unwrapped_h5.c_str(), "source_1", tmp_str.c_str());
	ret = conversion.read_str_from_h5(ifg_denoised_h5.c_str(), "source_2", tmp_str);
	ret = conversion.write_str_to_h5(ifg_unwrapped_h5.c_str(), "source_2", tmp_str.c_str());
	if (!conversion.read_array_from_h5(ifg_denoised_h5.c_str(), "flat_phase_coefficient", tmp))
		conversion.write_array_to_h5(ifg_unwrapped_h5.c_str(), "flat_phase_coefficient", tmp);
	ret = conversion.read_array_from_h5(ifg_denoised_h5.c_str(), "range_len", tmp);
	ret = conversion.write_array_to_h5(ifg_unwrapped_h5.c_str(), "range_len", tmp);
	ret = conversion.read_array_from_h5(ifg_denoised_h5.c_str(), "azimuth_len", tmp);
	ret = conversion.write_array_to_h5(ifg_unwrapped_h5.c_str(), "azimuth_len", tmp);

	processCallback(IPC, eCallbackType_UpdateCmplt, 90, "ifg unwrap finished!");

	/*-----------------------------------------------------------------------------*/
	/*                                高程反演                                     */
	/*-----------------------------------------------------------------------------*/

	//创建目录
	if (_mkdir(dem_dir.c_str()) < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "failed to create dem directory!");
		return -1;
	}

	dem_h5 = dem_dir + "\\" + tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase_denoised_unwrapped_dem.h5";
	
	ret = dem.dem_newton_iter(ifg_unwrapped_h5.c_str(), phase, working_directory.c_str(), para.iter_times);
	if (ret < 0)
	{
		processCallback(IPC, eCallbackType_MsgError, 0, "dem_newton_iter(): failed to get dem!");
		return -1;
	}
	ret = conversion.creat_new_h5(dem_h5.c_str());
	ret = conversion.write_array_to_h5(dem_h5.c_str(), "dem", phase);
	ret = conversion.read_str_from_h5(ifg_unwrapped_h5.c_str(), "source_1", tmp_str);
	ret = conversion.write_str_to_h5(dem_h5.c_str(), "source_1", tmp_str.c_str());
	ret = conversion.read_str_from_h5(ifg_unwrapped_h5.c_str(), "source_2", tmp_str);
	ret = conversion.write_str_to_h5(dem_h5.c_str(), "source_2", tmp_str.c_str());
	ret = conversion.read_array_from_h5(ifg_unwrapped_h5.c_str(), "range_len", tmp);
	ret = conversion.write_array_to_h5(dem_h5.c_str(), "range_len", tmp);
	ret = conversion.read_array_from_h5(ifg_unwrapped_h5.c_str(), "azimuth_len", tmp);
	ret = conversion.write_array_to_h5(dem_h5.c_str(), "azimuth_len", tmp);

	//回传参数
	template_DEM_para_back* callback_para = new template_DEM_para_back();

	std::replace(dst_h5_master.begin(), dst_h5_master.end(), '\\', '/');
	std::replace(dst_h5_slave.begin(), dst_h5_slave.end(), '\\', '/');
	strcpy(callback_para->import_node_name, "import");
	strcpy(callback_para->import_master_name, tmp_master.c_str());
	strcpy(callback_para->import_master_file, dst_h5_master.c_str());
	strcpy(callback_para->import_slave_file, dst_h5_slave.c_str());
	strcpy(callback_para->import_slave_name, tmp_slave.c_str());


	std::replace(AOI_h5_master.begin(), AOI_h5_master.end(), '\\', '/');
	std::replace(AOI_h5_slave.begin(), AOI_h5_slave.end(), '\\', '/');
	strcpy(callback_para->AOI_node_name, "AOI");
	strcpy(callback_para->AOI_master_name, (tmp_master + "_cut").c_str());
	strcpy(callback_para->AOI_master_file, AOI_h5_master.c_str());
	strcpy(callback_para->AOI_slave_file, AOI_h5_slave.c_str());
	strcpy(callback_para->AOI_slave_name, (tmp_slave + "_cut").c_str());


	std::replace(regis_h5_master.begin(), regis_h5_master.end(), '\\', '/');
	std::replace(regis_h5_slave.begin(), regis_h5_slave.end(), '\\', '/');
	strcpy(callback_para->regis_node_name, "coregistration");
	strcpy(callback_para->regis_master_name, (tmp_master + "_cut_regis").c_str());
	strcpy(callback_para->regis_master_file, regis_h5_master.c_str());
	strcpy(callback_para->regis_slave_file, regis_h5_slave.c_str());
	strcpy(callback_para->regis_slave_name, (tmp_slave + "_cut_regis").c_str());


	std::replace(ifg_h5.begin(), ifg_h5.end(), '\\', '/');
	strcpy(callback_para->ifg_node_name, "ifg");
	strcpy(callback_para->ifg_name, (tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase").c_str());
	if (para.ifg_b_coh)
	{
		strcpy(callback_para->ifg_coherence_name, (tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_coh").c_str());
	}
	strcpy(callback_para->ifg_file, ifg_h5.c_str());


	std::replace(ifg_denoised_h5.begin(), ifg_denoised_h5.end(), '\\', '/');
	strcpy(callback_para->denoise_node_name, "denoise");
	strcpy(callback_para->denoise_ifg_name, (tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase_denoised").c_str());
	strcpy(callback_para->denoise_ifg_file, ifg_denoised_h5.c_str());


	std::replace(ifg_unwrapped_h5.begin(), ifg_unwrapped_h5.end(), '\\', '/');
	strcpy(callback_para->unwrap_node_name, "unwrap");
	strcpy(callback_para->unwrap_ifg_name, (tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase_denoised_unwrapped").c_str());
	strcpy(callback_para->unwrap_ifg_file, ifg_unwrapped_h5.c_str());
	

	std::replace(dem_h5.begin(), dem_h5.end(), '\\', '/');
	strcpy(callback_para->dem_node_name, "dem");
	strcpy(callback_para->dem_name, (tmp_master + "_cut_regis_" + tmp_slave + "_cut_regis_phase_denoised_unwrapped_dem").c_str());
	strcpy(callback_para->dem_file, dem_h5.c_str());

	processCallback(IPC, eCallbackType::eCallbackType_MsgFinished, 100, "dem get!", callback_para);
	delete callback_para;
	return 0;
}

