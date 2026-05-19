#ifndef MYTHREAD_H
#define MYTHREAD_H
#include<QMetaType>
#include <QObject>
#include<QDebug>
#include<QThread>
#include<qmutex.h>
#include<FormatConversion.h>
#include<qstandarditemmodel.h>
#include<QDir>
class MyThread : public QObject
{
	Q_OBJECT
	XMLFile* DOC;
public:
	MyThread(QObject* parent = nullptr);
	~MyThread();
public slots:
	void Import();
	/** @brief 导入sentinel数据
	* @param PODFile                      哨兵一号精轨数据
	* @param manifest_file                哨兵一号IW数据产品manifest文件（带绝对路径）
	* @param subswath                     子带选择
	* @param polarization                 极化方式
	* @param project_path                 工程绝对路径
	* @param folder                       储存结果节点名
	* @param filename                     储存结果文件名（不带路径）
	* @param project_name                 工程文件名（不带路径）
	* @param model                        treeview结构
	*/
	void import_sentinel(
		QString PODFile,
		QString manifest_file,
		QString subswath,
		QString polarization,
		QString project_path,
		QString folder,
		QString filename,
		QString project_name,
		QStandardItemModel* model
	);
	/*@brief 批量导入哨兵数据
	* @param original_namelist            原始文件
	* @param import_namelist              导入文件
	* @param subswath                     子带
	* @param polarization                 极化
	* @param savepath                     保存路径
	* @param dst_node                     目标节点名
	* @param dst_project                  目标工程名
	* @param model                        treeview模型
	*/
	void import_sentinel_patch(
		vector<QString> original_namelist,
		vector<QString> import_namelist,
		QString subswath,
		QString polarization,
		QString savepath,
		QString dst_node,
		QString dst_project,
		QStandardItemModel* model
	);
	/** @brief 导入TerraSAR数据
	* @param polarization                 极化方式
	* @param xml_filename                 xml文件名
	* @param project_path                 工程绝对路径
	* @param folder                       储存结果节点名
	* @param filename                     储存结果文件名（不带路径）
	* @param project_name                 工程文件名（不带路径）
	* @param model                        treeview结构
	*/
	void import_TSX(
		QString polarization,
		QString xml_filename,
		QString project_path,
		QString folder,
		QString filename,
		QString project_name,
		QStandardItemModel* model
	);
	/*@brief 批量导入TerraSAR数据
	* @param polarization                极化方式
	* @param savepath                    保存路径
	* @param original_file_list          原始文件
	* @param import_namelist             导入文件名
	* @param dst_node                    目标节点
	* @param dst_project                 目标工程
	* @param copy                        treeview模型
	*/
	void import_TSX_patch(
		QString polarization,
		QString savepath,
		vector<QString> original_file_list,
		vector<QString> import_namelist,
		QString dst_node,
		QString dst_project,
		QStandardItemModel* model
	);
	/*@brief 批量导入COSMOS-SkyMed数据
	* @param savepath                    保存路径
	* @param original_file_list          原始文件
	* @param import_namelist             导入文件名
	* @param dst_node                    目标节点
	* @param dst_project                 目标工程
	* @param copy                        treeview模型
	*/
	void import_GenericSAR(
		QString xml_filename,
		QString project_path,
		QString folder,
		QString filename,
		QString project_name,
		QStandardItemModel* model
	);
	/*@brief 批量导入TerraSAR数据
	* @param polarization                极化方式
	* @param savepath                    保存路径
	* @param original_file_list          原始文件
	* @param import_namelist             导入文件名
	* @param dst_node                    目标节点
	* @param dst_project                 目标工程
	* @param copy                        treeview模型
	*/
	void import_GenericSAR_patch(
		QString savepath,
		vector<QString> original_file_list,
		vector<QString> import_namelist,
		QString dst_node,
		QString dst_project,
		QStandardItemModel* model
	);
	/*@brief 批量导入COSMOS-SkyMed数据
	* @param savepath                    保存路径
	* @param original_file_list          原始文件
	* @param import_namelist             导入文件名
	* @param dst_node                    目标节点
	* @param dst_project                 目标工程
	* @param copy                        treeview模型
	*/
	void import_CSK_patch(
		QString savepath,
		vector<QString> original_file_list,
		vector<QString> import_namelist,
		QString dst_node,
		QString dst_project,
		QStandardItemModel* model
	);
	/*@brief 批量导入ALOS2数据
	* @param savepath                    保存路径
	* @param IMG_file_list               原始文件(IMG文件)
	* @param LED_file_list2              原始文件(LED文件)
	* @param import_namelist             导入文件名
	* @param dst_node                    目标节点
	* @param dst_project                 目标工程
	* @param copy                        treeview模型
	*/
	void import_ALOS2_patch(
		QString savepath,
		vector<QString> IMG_file_list,
		vector<QString> LED_file_list,
		vector<QString> import_namelist,
		QString dst_node,
		QString dst_project,
		QStandardItemModel* model
	);
	void ShowImage(QString, QString, QString);
	/*@brief 经纬度裁剪算法
	* @param 
	*/
	void Cut(QList<double>, QString, QString, QString, QString, QStandardItemModel*);
	/*@brief 框选裁剪算法
	* @param
	*/
	void Cut2(
		double h5_left,
		double h5_right,
		double h5_top,
		double h5_bottom,
		QString save_path,
		QString project_name,
		QString node_name,
		QString file,
		QStandardItemModel* model
	);
	void Regis(QList<int>, QString, QString, QString, QString, QStandardItemModel*);
	/*@brief DEM辅助配准
	* @param masterIndex                主图像序号(从1开始)
	* @param savepath                   工程保存路径
	* @param project                    工程名
	* @param srcNode                    源数据节点
	* @param dstNode                    目标节点
	* @param model                      树模型
	*/
	void DEMAssistCoregistration(
		int masterIndex,
		QString savepath,
		QString project,
		QString srcNode,
		QString dstNode,
		QStandardItemModel* model
	);
	/*@brief 哨兵一号数据burst拼接处理
	* @param savePath                   保存路径
	* @param dstProject                 目标工程
	* @param srcNode                    源节点
	* @param dstNode                    目标节点
	* @param treeModel                  树模型
	*/
	void S1_Deburst(
		QString savePath,
		QString dstProject,
		QString srcNode,
		QString dstNode,
		QStandardItemModel* model
	);
	/*@brief 哨兵一号后向地理编码配准
	* @param images_number              图像幅数
	* @param masterIndex                主图序号（从1开始）
	* @param savePath                   保存路径
	* @param dstProject                 目标工程
	* @param srcNode                    源节点
	* @param dstNode                    目标节点
	* @param treeModel                  树模型
	* @param b_ESD                      是否采用ESD
	*/
	void S1_TOPS_BackGeocoding(
		int images_number,
		int masterIndex,
		QString savePath,
		QString dstProject,
		QString srcNode,
		QString dstNode,
		QStandardItemModel* model,
		bool b_ESD = true
	);
	/*@brief 配准后SAR图像堆栈去参考相位处理（包括平地和地形相位）
	* @param masterIndex                主图像序号（从1开始）
	* @param project_name               工程名
	* @param src_node                   待处理数据节点名
	* @param dst_node                   结果保存节点名
	* @param model                      工程管理树模型
	*/
	void SLC_deramp(
		int masterIndex,
		QString project_name,
		QString src_node,
		QString dst_node,
		QStandardItemModel* model
	);
	/*@brief 估计基线分布并计算小基线集干涉组合矩阵
	* @param masterIndex               主图像序号（从1开始）
	* @param project_name              工程名
	* @param src_node                  待处理数据节点
	* @param model                     工程管理树模型
	*/
	void Baseline_Formation(
		int masterIndex,
		QString project_name,
		QString src_node,
		QStandardItemModel* model
	);
	/*@brief 小基线集时间序列分析
	* @param temporal_thresh_low        时间基线低阈值（day）
	* @param temporal_thresh            时间基线高阈值（day）
	* @param spatial_thresh             空间基线阈值（m）
	* @param multilook_rg               距离向多视倍数
	* @param multilook_az               方位向多视倍数
	* @param unwrap_method              解缠方法（1：Delaunay_MCF，2：SNAPHU，3：MCF）
	* @param Goldstein_alpha            Goldstein滤波强度
	* @param coherence_thresh           高相干阈值
	* @param temporal_coherence_thresh  模型相关系数阈值
	* @param refinement_coh_thresh      轨道精炼和重去平控制点筛选相关系数阈值
	* @param refinemen_def_thresh       轨道精炼和重去平控制点筛选形变速率阈值（m/year）
	* @param project                    工程名
	* @param srcNode                    数据节点名
	* @param dstNode                    目标节点
	* @param csv_path                   csv文件保存路径
	* @param model                      工程管理树模型
	*/
	void SBAS_time_series(
		double temporal_thresh_low,
		double temporal_thresh,
		double spatial_thresh,
		int multilook_rg,
		int multilook_az,
		int unwrap_method,
		double alpha,
		double coherence_thresh,
		double temporal_coherence_thresh,
		double refinement_coh_thresh,
		double refinemen_def_thresh,
		QString project,
		QString srcNode,
		QString dstNode,
		QString csv_path,
		QStandardItemModel* model
	);
	/*@brief SBAS时间序列分析参考点重选
	* @param project            目标工程
	* @param srcNode            目标数据节点
	* @param ref_row            参考点行数
	* @param ref_col            参考点列数
	* @param GCPs               重去平参考点
	* @param model              工程管理树模型
	*/
	void SBAS_reference_reselection(
		QString project,
		QString srcNode,
		int ref_row,
		int ref_col,
		QList<QPoint> GCPs,
		QStandardItemModel* model
	);
	/*@brief 地理编码
	* @param type              地理编码对象（1：干涉产品，2：SAR复图像）
	* @param multi_rg          距离向多视倍数（用于SAR图像地理编码）
	* @param multi_az          方位向多视倍数（用于SAR图像地理编码）
	* @param project           工程名
	* @param srcNode           源节点名
	* @param dstNode           目标节点名
	* @param model             工程管理树模型
	*/
	void Geocoding(
		int type,
		int multi_rg,
		int multi_az,
		QString project,
		QString srcNode,
		QString dstNode,
		QStandardItemModel* model
	);
	/*@brief 哨兵一号干涉相位子带拼接
	* @param index1            子带1干涉相位图像序号（1-based）
	* @param index2            子带2干涉相位图像序号
	* @param index3            子带3干涉相位图像序号
	* @param project_name      工程文件名
	* @param srcNode1          子带1干涉相位数据节点
	* @param srcNode2          子带2干涉相位数据节点
	* @param srcNode3          子带3干涉相位数据节点
	* @param dstNode           目标节点
	* @param model             工程管理树模型
	*/
	void S1_swath_merge(
		int index1,
		int index2,
		int index3,
		QString project_name,
		QString srcNode1,
		QString srcNode2,
		QString srcNode3,
		QString dstNode,
		QStandardItemModel* model
	);
	/*@brief 哨兵一号相邻frame单视复图像拼接
	* @param index1            子带1干涉相位图像序号（1-based）
	* @param index2            子带2干涉相位图像序号
	* @param project_name      工程文件名
	* @param srcNode1          子带1干涉相位数据节点
	* @param srcNode2          子带2干涉相位数据节点
	* @param dstNode           目标节点
	* @param model             工程管理树模型
	*/
	void S1_frame_merge(
		int index1,
		int index2,
		QString project_name,
		QString srcNode1,
		QString srcNode2,
		QString dstNode,
		QStandardItemModel* model
	);
	void Baseline_Estimate(int index, QString project_name, QString dst_node, const QStandardItemModel*);
	void Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence, int master_index, int win_r, int win_c,
		int multilook_row, int multilook_col, QString, QString, QString, QString, QStandardItemModel*);
	void Denoise(QList<int>, double, QString, QString, QString, QString, QStandardItemModel*);
	void QUnwrap(int, double,  QString, QString, QString, QString, QStandardItemModel*);
	void QDem(int, int, QString, QString, QString, QString, QStandardItemModel*);
	void StopProcess();
signals:
	void updateProcess(int, QString);
	void endProcess();
	/*传输错误信息*/
	void errorProcess(QString error_msg);
	void sendModel(QStandardItemModel*);
	void sendBL(QList<double>temporal_baseline, QList<double>spatial_baseline, int index);
private:
	QMutex lock;
	bool stop_flag;
	int Registration_copy(
		vector<string>& SAR_images,
		vector<string>& SAR_images_out,
		Mat& offset_row_out,
		Mat& offset_col_out,
		int Master_index, 
		int interp_times,
		int blocksize
	);
	int complex_coherence(
		const ComplexMat& master_image,
		const ComplexMat& slave_image,
		int est_wndsize_rg,
		int est_wndsize_az,
		Mat& coherence);
	int change_suffix(const char* input, QString output_str, QString old_suffix, QString new_suffix);
};

#endif // MYTHREAD_H