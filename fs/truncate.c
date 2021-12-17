/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */
#include <linux/sched.h>        								// 调度程序头文件，定义了任务结构task_struct、任务0数据等。

#include <sys/stat.h>           								// 文件状态头文件。含有文件或文件系统状态结构stat{}和常量。

// 释放所有一次间接块。（内部函数）
// 参数dev是文件系统所有设备的设备号；block是逻辑块号。成功则返回1，否则返回0。
static int free_ind(int dev, int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;
	int block_busy;

	// 首先判断参数的有效性。如果逻辑块号为0,则返回。然后读取一次间接块，并释放其上表明使用的所有逻辑块，然后
	// 释放该一次间接块的缓冲块。函数free_block()用于释放设备上指定逻辑块号的磁盘块（fs/bitmap.c）。
	if (!block)
		return 1;
	block_busy = 0;
	if (bh = bread(dev, block)) {
		p = (unsigned short *) bh->b_data;              		// 指向缓冲块数据区。
		for (i = 0; i < 512; i++, p++)                         	// 每个逻辑块上可有512个块号。
			if (*p)
				if (free_block(dev, *p)) {       				// 释放指定的设备逻辑块。
					*p = 0;                 					// 清零。
					bh->b_dirt = 1;         					// 设置已修改标志。
				} else
					block_busy = 1;         					// 设置逻辑块没有释放标志。
		brelse(bh);                                     		// 然后释放间接块占用的缓冲块。
	}
	// 最后释放设备上的一次间接块。但如果其中有逻辑块没有被释放，则返回0（失败）。
	if (block_busy)
		return 0;
	else
		return free_block(dev, block);                   		// 成功则返回1,否则返回0.
}

// 释放所有二次间接块。
// 参数dev是文件系统所在设备的设备号；block是逻辑块号。
static int free_dind(int dev, int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;
	int block_busy;                                         	// 在逻辑块没有被释放的标志。

	// 首先判断参数的有效性。如果逻辑块号为0,则返回。然后读取二次间接块的一级块，并释放其上表明使用的所有逻辑块，
	// 然后释放该一级块的缓冲块。
	if (!block)
		return 1;
	block_busy = 0;
	if (bh = bread(dev, block)) {
		p = (unsigned short *) bh->b_data;              		// 指向缓冲块数据区。
		for (i = 0; i < 512; i++, p++)                         	// 每个逻辑块上可连接512个二级块。
			if (*p)
				if (free_ind(dev, *p)) {         				// 释放所有一次间接块。
					*p = 0;                 					// 清零。
					bh->b_dirt = 1;         					// 设置已修改标志。
				} else
					block_busy = 1;         					// 设置逻辑块没有释放标志。
		brelse(bh);                                     		// 释放二次间接块占用的缓冲块。
	}
	// 最后释放设备上的二次间接块。但如果其中有逻辑块没有被释放，则返回0（失败）。
	if (block_busy)
		return 0;
	else
		return free_block(dev, block);							// 最后释放存放第一间接块的逻辑块
}

// 截断文件数据函数。
// 将节点对应的文件长度减0,并释放战胜的设备空间。
void truncate(struct m_inode * inode)
{
	int i;
	int block_busy;                 							// 有逻辑块没有被释放的标志。

	// 首先判断指定i节点有效性。如果不是常规文件、目录文件或链接项，则返回。
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return;
	// 然后释放i节点的7个直接逻辑块，并将这7个逻辑块项全置零。函数free_block()用于释放设备上指定逻辑块的磁盘块
	// （fs/bitmap.c）。若有逻辑块忙而没有被释放则置块忙标志block_busy。
repeat:
	block_busy = 0;
	for (i = 0; i < 7; i++)
		if (inode->i_zone[i]) {                 				// 如果块号不为0，则释放之。
			if (free_block(inode->i_dev, inode->i_zone[i]))
				inode->i_zone[i] = 0;     						// 块指针置0。
			else
				block_busy = 1;         						// 若没有释放掉则置标志。
		}
	if (free_ind(inode->i_dev, inode->i_zone[7]))    			// 释放所有一次间接块。
		inode->i_zone[7] = 0;                   				// 块指针置0。
	else
		block_busy = 1;                         				// 若没有释放掉则置标志。
	if (free_dind(inode->i_dev, inode->i_zone[8]))   			// 释放所有二次间接块。
		inode->i_zone[8] = 0;                   				// 块指针置0。
	else
		block_busy = 1;                         				// 若没有释放掉则置标志。
	// 此后设置i节点已修改标志，并且如果还有逻辑块由于 “忙”而没有被释放，则把当前进程运行时间片置0,以让当前进程先被
	// 切换去运行其他进程，稍等一会再重新执行释放操作。
	inode->i_dirt = 1;
	if (block_busy) {
		current->counter = 0;           						// 当前进程时间片置0。
		schedule();
		goto repeat;
	}
	inode->i_size = 0;                      					// 文件大小置零。
	// 最后重新置文件修改时间和i节点改变时间为当前时间。宏CURRENT_TIME定义在头文件include/linux/sched.h中，定义
	// 为（startup_time+jiffies/HZ）。用于取得从1970:0:0:0开始到现在为止经过的秒数。
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

