/*++

  INTEL CONFIDENTIAL
  Copyright 2016 - 2017 Intel Corporation

  The source code contained or described  herein and all documents related to
  the  source  code  ("Material")  are  owned  by  Intel  Corporation  or its
  suppliers  or  licensors.  Title   to  the  Material   remains  with  Intel
  Corporation or  its suppliers  and licensors.  The Material  contains trade
  secrets  and  proprietary  and  confidential  information  of Intel  or its
  suppliers and licensors.  The Material is protected  by worldwide copyright
  and trade secret laws and treaty provisions. No part of the Material may be
  used,   copied,   reproduced,   modified,   published,   uploaded,  posted,
  transmitted,  distributed, or  disclosed in  any way  without Intel's prior
  express written permission.

  No license under any patent, copyright,  trade secret or other intellectual
  property  right  is  granted to  or conferred  upon  you by  disclosure  or
  delivery of the  Materials, either  expressly, by  implication, inducement,
  estoppel or otherwise. Any license  under such intellectual property rights
  must be express and approved by Intel in writing.

  --*/

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <csignal>
#include <fstream>
#include <thread>
#include "gtest/gtest.h"
#include <opae/access.h>
#include <opae/enum.h>
#include <opae/manage.h>
#include <opae/manage.h>
#include <opae/mmio.h>
#include <opae/properties.h>
#include <opae/types_enum.h>
#ifdef BUILD_ASE
#include "ase/api/src/types_int.h"
#else
#include "types_int.h"
#endif
#include "common_test.h"
#include "safe_string/safe_string.h"

#define SYSFS_PATH_MAX 256
using namespace std;


#define DSM_STATUS_TEST_COMPLETE	0x40
#define NLB_TEST_MODE_LPBK1		0x000
#define NUM_MODE0_CLS			40

GlobalOptions GlobalOptions::sm_Instance;
GlobalOptions& GlobalOptions::Instance() { return GlobalOptions::sm_Instance; }

namespace common_test {
/**
 * @brief      Determines whether or not the input string is a
 *             directory.
 *
 * @param[in]  path  The path string to test
 *
 * @return     True if directory, false otherwise.
 */
bool check_path_is_dir(const char* path) {
	struct stat statbuf;

	stat(path, &statbuf);

	if (S_ISDIR(statbuf.st_mode)) {
	return true;
	}  // directory

	else {
	return false;
	}  // file
}

/**
 * @brief      Converts return codes to string values.
 *
 * @param[in]  result  The FPGA result to print
 * @param[in]  line    Typically, source file and line number
 *
 * @return     Returns false on error code from OPAE library, true
 *             otherwise.
 */
bool checkReturnCodes(fpga_result result, string line) {
	auto pid = getpid();

	switch (result) {
	case FPGA_OK:
		// cout << "fpga ok\t"
		//      << "pid:  " << pid << endl;
		return true;

	case FPGA_INVALID_PARAM:
		cout << endl
			<< "fpga invalid param\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_BUSY:
		cout << endl
			<< "fpga busy\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_EXCEPTION:
		cout << endl
			<< "fpga exception\t"
			<< "pid:  " << pid << " ... " << line << endl;
		// raise(SIGINT);
		return false;
	case FPGA_NOT_FOUND:
		cout << endl
			<< "fpga not found\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_NO_MEMORY:
		cout << endl
			<< "fpga no memory\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_NOT_SUPPORTED:
		cout << endl
			<< "fpga not supported\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_NO_DRIVER:
		cout << endl
			<< "fpga no driver\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_NO_DAEMON:
		cout << endl
			<< "fpga no daemon\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_NO_ACCESS:
		cout << endl
			<< "fpga no access\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	case FPGA_RECONF_ERROR:
		cout << endl
			<< "fpga reconf error\t"
			<< "pid:  " << pid << " ... " << line << endl;
		return false;
	}

	return true;
}

/**
 * @brief      Reads from the sysfs path.
 *
 * @param[in]  path  The sysfs path
 * @param[out] u     Out param in which to store the read value
 *
 * @return     Returns OPAE library  success or failure code.
 */
fpga_result sysfs_read_64(const char* path, uint64_t* u) {
	int fd = -1;
	int res = 0;
	char buf[SYSFS_PATH_MAX] = {0};
	int b = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("open(%s) failed", path);
		return FPGA_NOT_FOUND;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		printf("seek failed");

		goto out_close;
	}

	do {
		res = read(fd, buf + b, sizeof(buf) - b);
		if (res <= 0) {
			printf("Read from %s failed", path);

			goto out_close;
		}
		b += res;
		if ((b > sizeof(buf)) || (b <= 0)) {
			printf("Unexpected size reading from %s", path);

			goto out_close;
		}
	} while (buf[b - 1] != '\n' && buf[b - 1] != '\0' && b < sizeof(buf));

	// erase \n
	buf[b - 1] = 0;

	*u = strtoull(buf, NULL, 0);

	if (close(fd)) {
		perror("close");
	}
	return FPGA_OK;

out_close:
	if (close(fd)) {
		perror("close");
	} else {
		fd = -1;
	}

	return FPGA_NOT_FOUND;
}

/**
 * @brief      Writes value to sysfs.
 *
 * @param[in]  path  The sysfs path
 * @param[in]  u     Value to write
 * @param[in]  B     Base (HEX or DEC)
 *
 * @return     Return OPAE library success or failure code.
 */

fpga_result sysfs_write_64(const char* path, uint64_t u, base B) {
	int res = 0;
	char buf[SYSFS_PATH_MAX] = {0};
	int b = 0;
	fpga_result retval = FPGA_OK;

	int fd = open(path, O_WRONLY);

	if (fd < 0) {
		printf("open: %s", strerror(errno));
		retval = FPGA_NOT_FOUND;
		goto out_close;
	}

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		printf("seek: %s", strerror(errno));
		retval = FPGA_NOT_FOUND;
		goto out_close;
	}

	switch (B) {
		// write hex value
	case HEX:
		snprintf(buf, sizeof(buf), "%lx", u);
		break;

		// write dec value
	case DEC:
		snprintf(buf, sizeof(buf), "%ld", u);
		break;
	}

	do {
		res = write(fd, buf + b, sizeof(buf) - b);

		if (res <= 0) {
			printf("Failed to write");
			retval = FPGA_NOT_FOUND;
			goto out_close;
		}

		b += res;

		if (b > sizeof(buf) || b <= 0) {
			printf("Unexpected size reading from %s", path);
			retval = FPGA_NOT_FOUND;
			goto out_close;
		}

	} while (buf[b - 1] != '\n' && buf[b - 1] != '\0' && b < sizeof(buf));

	retval = FPGA_OK;
	goto out_close;

out_close:
	if (close(fd) < 0) {
		perror("close");
	} else {
		fd = -1;
	}
	return retval;
}

#ifndef BUILD_ASE
/**
 * @brief      Reads a sysfs value.
 *
 * @param[in]  feature  The feature being read
 * @param      value    The value being read
 * @param[in]  tok      The FPGA device/accelerator token
 *
 * @return     Returns OPAE library success or failure code.
 */
fpga_result read_sysfs_value(const char* feature, uint64_t* value,
                             fpga_token tok) {
	fpga_result result;
	char path[SYSFS_PATH_MAX];
	memset(path, 0, SYSFS_PATH_MAX);

	strcat_s(path, sizeof(path), ((_fpga_token*)tok)->sysfspath);
	strcat_s(path, sizeof(path), feature);
	checkReturnCodes(result = sysfs_read_64(path, value), LINE(__LINE__));
	return result;
}

/**
 * @brief      Writes a sysfs value.
 *
 * @param[in]  feature  The feature being written to
 * @param[in]  value    The value being written
 * @param[in]  tok      The FPGA device/accelerator token
 * @param[in]  B        The numeric base (HEX or DEC)
 *
 * @return     Returns OPAE library success or failure code.
 */
fpga_result write_sysfs_value(const char* feature, uint64_t value,
                              fpga_token tok, base B) {
	fpga_result result = FPGA_OK;
	char path[SYSFS_PATH_MAX] = {0};

	memset(path, 0, SYSFS_PATH_MAX);

	strcat_s(path, sizeof(path), ((_fpga_token*)tok)->sysfspath);
	strcat_s(path, sizeof(path), feature);

	switch (B) {
	case HEX:
		if (!checkReturnCodes(result = sysfs_write_64(path, value, HEX),
				      LINE(__LINE__))) {
			checkIOErrors(path, value);
		}
		break;

	case DEC:
		if (!checkReturnCodes(result = sysfs_write_64(path, value, DEC),
				      LINE(__LINE__))) {
			checkIOErrors(path, value);
		}
		break;
	}
	return result;
}

/**
 * @brief      Ensures the input feature path exists on the system.
 *
 * @param[in]  feature  The feature being tested
 * @param[in]  tok      The FPGA device/accelerator token
 *
 * @return     True if supported, false otherwise.
 */
bool feature_is_supported(const char* feature, fpga_token tok) {
	bool isDir = false;
	char path[SYSFS_PATH_MAX];
	memset(path, 0, SYSFS_PATH_MAX);

	strcat_s(path, sizeof(path), ((_fpga_token*)tok)->sysfspath);
	strcat_s(path, sizeof(path), feature);
	printf("FEATURE PATH:  %s \n", path);
	EXPECT_TRUE(isDir = check_path_is_dir(path));
	return isDir;
}
#endif

void printIOError(string line) {
	perror("IOERROR:  ");
	cout << ":  " << line << endl;
}

void checkIOErrors(const char* syspath, uint64_t value) {
	struct stat idstat;
	if (0 != (stat(syspath, &idstat))) cout << "File stat failed!!" << endl;
	printf("value:  %lx", value);
	printIOError(LINE(__LINE__));
}
}  // end namespace common_test
