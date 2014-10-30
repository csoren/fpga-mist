/*
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-11-22 - read/write multiple implemented

#ifdef __GNUC__
#include "AT91SAM7S256.h"
#include "stdio.h"
#include "string.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "errors.h"
#include "hardware.h"
#include "fat.h"
#include "hdd.h"
#include "hdd_internal.h"
#include "mmc.h"
#include "menu.h"
#include "fpga.h"
#include "config.h"
#include "debug.h"

// hardfile structure
hdfTYPE hdf[2];

static void RDBChecksum(unsigned long *p)
{
	unsigned long count=p[1];
	unsigned long c2;
	long result=0;
	p[2]=0;
	for(c2=0;c2<count;++c2)
		result+=p[c2];
	p[2]=(unsigned long)-result;
}


// If the hardfile doesn't have a RigidDiskBlock,
// we synthesize one.
static void FakeRDB(int unit,int block)
{
	int i;
	// Start by clearing the sector buffer
	for(i=0;i<512;++i)
		sector_buffer[i]=0;

	// If we're asked for LBA 0 we create an RDSK block, and if LBA 1, a PART block.
	switch(block)
	{
		case 0: // RDB
			{
				struct RigidDiskBlock *rdb=(struct RigidDiskBlock *)sector_buffer;
				rdb->rdb_ID = 'R'<<24 | 'D' << 16 | 'S' << 8 | 'K';
				
				rdb->rdb_Summedlongs=0x40;
				rdb->rdb_HostID=0x07;
				rdb->rdb_BlockBytes=0x200;
				rdb->rdb_Flags=0x12; // (Disk ID valid, no LUNs after this one)
				rdb->rdb_BadBlockList=0xffffffff; // We don't provide a bad block list
				rdb->rdb_PartitionList=1;
				rdb->rdb_FileSysHeaderList=0xffffffff;
				rdb->rdb_DriveInit=0xffffffff;
				rdb->rdb_Reserved1[0]=0xffffffff;
				rdb->rdb_Reserved1[1]=0xffffffff;
				rdb->rdb_Reserved1[2]=0xffffffff;
				rdb->rdb_Reserved1[3]=0xffffffff;
				rdb->rdb_Reserved1[4]=0xffffffff;
				rdb->rdb_Reserved1[5]=0xffffffff;
				rdb->rdb_Cylinders=hdf[unit].cylinders;
				rdb->rdb_Sectors=hdf[unit].sectors;
				rdb->rdb_Heads=hdf[unit].heads;
				rdb->rdb_Interleave=1;
				rdb->rdb_Park=rdb->rdb_Cylinders;
				rdb->rdb_WritePreComp=rdb->rdb_Cylinders;
				rdb->rdb_ReducedWrite=rdb->rdb_Cylinders;
				rdb->rdb_StepRate=3;
				rdb->rdb_RDBBlocksLo=0;
				rdb->rdb_RDBBlocksHi=1;
				rdb->rdb_LoCylinder=1;
				rdb->rdb_HiCylinder=rdb->rdb_Cylinders-1;
				rdb->rdb_CylBlocks=rdb->rdb_Heads * rdb->rdb_Sectors;
				rdb->rdb_AutoParkSeconds=0;
				rdb->rdb_HighRDSKBlock=1;
				strcpy(rdb->rdb_DiskVendor,"Do not ");
				strcpy(rdb->rdb_DiskProduct, "repartition!");

				RDBChecksum((unsigned long *)rdb);
			}
			break;
		case 1: // Partition
			{
				struct PartitionBlock *pb=(struct PartitionBlock *)sector_buffer;
				pb->pb_ID = 'P'<<24 | 'A' << 16 | 'R' << 8 | 'T';
			
				pb->pb_Summedlongs=0x40;
				pb->pb_HostID=0x07;
				pb->pb_Next=0xffffffff;
				pb->pb_Flags=0x1; // Bootable
				pb->pb_DevFlags=0;
				strcpy(pb->pb_DriveName," DH0");
				pb->pb_DriveName[0]=3;	// BCPL string

				pb->pb_Environment.de_TableSize=0x10;
				pb->pb_Environment.de_SizeBlock=0x80;
				pb->pb_Environment.de_Surfaces=hdf[unit].heads;
				pb->pb_Environment.de_SectorPerBlock=1;
				pb->pb_Environment.de_BlocksPerTrack=hdf[unit].sectors;
				pb->pb_Environment.de_Reserved=2;
				pb->pb_Environment.de_LowCyl=1;
				pb->pb_Environment.de_HighCyl=hdf[unit].cylinders-1;
				pb->pb_Environment.de_NumBuffers=30;
				pb->pb_Environment.de_MaxTransfer=0xffffff;
				pb->pb_Environment.de_Mask=0x7ffffffe;
				pb->pb_Environment.de_DosType=0x444f5301;

				RDBChecksum((unsigned long *)pb);
			}
			break;
			break;
		default:
			break;
	}
}


void IdentifyDevice(unsigned short *pBuffer, unsigned char unit)
{ // builds Identify Device struct
    char *p, i, x;
    unsigned long total_sectors = hdf[unit].cylinders * hdf[unit].heads * hdf[unit].sectors;

    memset(pBuffer, 0, 512);

	switch(hdf[unit].type)
	{
		case HDF_FILE | HDF_SYNTHRDB:
		case HDF_FILE:
			pBuffer[0] = 1 << 6; // hard disk
			pBuffer[1] = hdf[unit].cylinders; // cyl count
			pBuffer[3] = hdf[unit].heads; // head count
			pBuffer[6] = hdf[unit].sectors; // sectors per track
			// FIXME - can get serial no from card itself.
			memcpy((char*)&pBuffer[10], "1234567890ABCDEFGHIJ", 20); // serial number - byte swapped
			memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
			p = (char*)&pBuffer[27];
			// FIXME - likewise the model name can be fetched from the card.
			if(hdf[unit].type & HDF_SYNTHRDB)
			{
				memcpy(p, "DON'T                                   ", 40);
				p += 8;
				memcpy(p, "REPARTITION!    ", 16);
			}
			else
			{
				memcpy(p, "YAQUBE                                  ", 40); // model name - byte swapped
				p += 8;
				if (config.hardfile[unit].long_name[0])
				{
					for (i = 0; (x = config.hardfile[unit].long_name[i]) && i < 16; i++) // copy file name as model name
						p[i] = x;
				}
				else
				{
					memcpy(p, config.hardfile[unit].name, 8); // copy file name as model name
				}
			}
		//    SwapBytes((char*)&pBuffer[27], 40); //not for 68000
			break;
		case HDF_CARD:
		case HDF_CARDPART0:
		case HDF_CARDPART1:
		case HDF_CARDPART2:
		case HDF_CARDPART3:
			pBuffer[0] = 1 << 6; // hard disk
			pBuffer[1] = hdf[unit].cylinders; // cyl count
			pBuffer[3] = hdf[unit].heads; // head count
			pBuffer[6] = hdf[unit].sectors; // sectors per track
			// FIXME - can get serial no from card itself.
			memcpy((char*)&pBuffer[10], "TC64MiniMigSD0      ", 20); // serial number - byte swapped
			pBuffer[23]+=hdf[unit].type-HDF_CARD;
			memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
			p = (char*)&pBuffer[27];
			// FIXME - likewise the model name can be fetched from the card.
			memcpy(p, "YAQUBE                                  ", 40); // model name - byte swapped
			p += 8;
			if(hdf[unit].type==HDF_CARD)
				memcpy(p, "SD/MMC Card", 11); // copy file name as model name
			else
			{
				memcpy(p, "Card Part 1", 11); // copy file name as model name
				p[10]+=hdf[unit].partition;
			}
			//    SwapBytes((char*)&pBuffer[27], 40); //not for 68000
			break;
	}

    pBuffer[47] = 0x8010; //maximum sectors per block in Read/Write Multiple command
    pBuffer[53] = 1;
    pBuffer[54] = hdf[unit].cylinders;
    pBuffer[55] = hdf[unit].heads;
    pBuffer[56] = hdf[unit].sectors;
    pBuffer[57] = (unsigned short)total_sectors;
    pBuffer[58] = (unsigned short)(total_sectors >> 16);
}

unsigned long chs2lba(unsigned short cylinder, unsigned char head, unsigned short sector, unsigned char unit)
{
    return(cylinder * hdf[unit].heads + head) * hdf[unit].sectors + sector - 1;
}

void WriteTaskFile(unsigned char error, unsigned char sector_count, unsigned char sector_number, unsigned char cylinder_low, unsigned char cylinder_high, unsigned char drive_head)
{
    EnableFpga();

    SPI(CMD_IDE_REGS_WR); // write task file registers command
    SPI(0x00);
    SPI(0x00); // dummy
    SPI(0x00);
    SPI(0x00); // dummy
    SPI(0x00);



    SPI(0x00); // dummy

    SPI(0x00);
    SPI(0x00);
    SPI(error); // error
    SPI(0x00);
    SPI(sector_count); // sector count
    SPI(0x00);
    SPI(sector_number); //sector number
    SPI(0x00);
    SPI(cylinder_low); // cylinder low
    SPI(0x00);
    SPI(cylinder_high); // cylinder high
    SPI(0x00);
    SPI(drive_head); // drive/head

    DisableFpga();
}

void WriteStatus(unsigned char status)
{
    EnableFpga();

    SPI(CMD_IDE_STATUS_WR);
    SPI(status);
    SPI(0x00);
    SPI(0x00);
    SPI(0x00);
    SPI(0x00);

    DisableFpga();
}

void HandleHDD(unsigned char c1, unsigned char c2)
{
    unsigned short id[256];
    unsigned char  tfr[8];
    unsigned short i;
    unsigned short sector;
    unsigned short cylinder;
    unsigned char  head;
    unsigned char  unit;
    unsigned short sector_count;
    unsigned short block_count;

    if (c1 & CMD_IDECMD)
    {
        DISKLED_ON;
        EnableFpga();
        SPI(CMD_IDE_REGS_RD); // read task file registers
        SPI(0x00);
        SPI(0x00);
        SPI(0x00);
        SPI(0x00);
        SPI(0x00);
        for (i = 0; i < 8; i++)
        {
            SPI(0);
            tfr[i] = SPI(0);
        }
        DisableFpga();

        unit = tfr[6] & 0x10 ? 1 : 0; // master/slave selection

        if (0)
        {
            hdd_debugf("IDE:");
            for (i = 1; i < 7; i++)
                hdd_debugf("%02X.",tfr[i]);
            hdd_debugf("%02X\r", tfr[7]);
        }
        if ((tfr[7] & 0xF0) == ACMD_RECALIBRATE) // Recalibrate 0x10-0x1F (class 3 command: no data)
        {
            hdd_debugf("Recalibrate\r");
            WriteTaskFile(0, 0, 1, 0, 0, tfr[6] & 0xF0);
            WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
        }
        else if (tfr[7] == ACMD_IDENTIFY_DEVICE) // Identify Device
        {
            hdd_debugf("Identify Device\r");
            IdentifyDevice(id, unit);
            WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
            WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
            EnableFpga();
            SPI(CMD_IDE_DATA_WR); // write data command
            SPI(0x00);
            SPI(0x00);
            SPI(0x00);
            SPI(0x00);
            SPI(0x00);
            for (i = 0; i < 256; i++)
            {
                SPI((unsigned char)id[i]);
                SPI((unsigned char)(id[i] >> 8));
            }
            DisableFpga();
            WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
        }
        else if (tfr[7] == ACMD_INITIALIZE_DEVICE_PARAMETERS) // Initiallize Device Parameters
        {
            hdd_debugf("Initialize Device Parameters\r");
            hdd_debugf("IDE:");
            for (i = 1; i < 7; i++)
                hdd_debugf("%02X.", tfr[i]);
            hdd_debugf("%02X\r", tfr[7]);
            WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
            WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
        }
        else if (tfr[7] == ACMD_READ_SECTORS) // Read Sectors
        {
			long lba;

            sector = tfr[3];
            cylinder = tfr[4] | (tfr[5] << 8);
            head = tfr[6] & 0x0F;
            sector_count = tfr[2];
            if (sector_count == 0)
               sector_count = 0x100;

			switch(hdf[unit].type)
			{
				case HDF_FILE | HDF_SYNTHRDB:
				case HDF_FILE:
					lba=chs2lba(cylinder, head, sector, unit);
				    if (hdf[unit].file.size)
				        HardFileSeek(&hdf[unit], (lba+hdf[unit].offset) < 0 ? 0 : lba+hdf[unit].offset);

				    while (sector_count)
				    {
//				 decrease sector count
						if(sector_count!=1)
						{
							if (sector == hdf[unit].sectors)
							{
								sector = 1;
								head++;
								if (head == hdf[unit].heads)
								{
									head = 0;
									cylinder++;
								}
							}
							else
								sector++;
						}
			
						if((lba+hdf[unit].offset)<0)
						{
							FakeRDB(unit,lba);

							WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
				            WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
				            EnableFpga();
				            SPI(CMD_IDE_DATA_WR); // write data command
				            SPI(0x00);
				            SPI(0x00);
				            SPI(0x00);
				            SPI(0x00);
				            SPI(0x00);
				            for (i = 0; i < 512; i++)
				            {
				                SPI(sector_buffer[i]);
				            }
				            DisableFpga();
				            WriteStatus(sector_count==1 ? IDE_STATUS_IRQ|IDE_STATUS_END : IDE_STATUS_IRQ);
						}
						else
						{
				            WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
							WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);

					        while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer

						    WriteStatus(IDE_STATUS_IRQ);

						    if (hdf[unit].file.size)
						    {
			//                    FileRead(&hdf[unit].file, NULL);
						        FileRead(&hdf[unit].file, 0);
						        FileSeek(&hdf[unit].file, 1, SEEK_CUR);
						    }
						}
						lba++;
					    sector_count--; // decrease sector count
				    }
					break;
				case HDF_CARD:
				case HDF_CARDPART0:
				case HDF_CARDPART1:
				case HDF_CARDPART2:
				case HDF_CARDPART3:
					{
			            WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
				        lba=chs2lba(cylinder, head, sector, unit)+hdf[unit].offset;
					    while (sector_count)
					    {
//				 decrease sector count
							if(sector_count!=1)
							{
								if (sector == hdf[unit].sectors)
								{
									sector = 1;
									head++;
									if (head == hdf[unit].heads)
									{
										head = 0;
										cylinder++;
									}
								}
								else
									sector++;
							}

							WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);

					        while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer
					        WriteStatus(IDE_STATUS_IRQ);
							MMC_Read(lba,0);
							++lba;
							--sector_count;
						}
					}
					break;
			}
        }
        else if (tfr[7] == ACMD_SET_MULTIPLE_MODE) // Set Multiple Mode
        {
            hdf[unit].sectors_per_block = tfr[2];

            hdd_debugf("Set Multiple Mode\r");
            hdd_debugf("IDE:");
            for (i = 1; i < 7; i++)
                hdd_debugf("%02X.", tfr[i]);
            hdd_debugf("%02X\r", tfr[7]);

            WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
        }
        else if (tfr[7] == ACMD_READ_MULTIPLE) // Read Multiple Sectors (multiple sector transfer per IRQ)
        {
			long lba;
            WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type

            sector = tfr[3];
            cylinder = tfr[4] | (tfr[5] << 8);
            head = tfr[6] & 0x0F;
            sector_count = tfr[2];
            if (sector_count == 0)
               sector_count = 0x100;

			switch(hdf[unit].type)
			{
				case HDF_FILE | HDF_SYNTHRDB:
				case HDF_FILE:
					lba=chs2lba(cylinder, head, sector, unit);
				    if (hdf[unit].file.size)
				        HardFileSeek(&hdf[unit], (lba+hdf[unit].offset) < 0 ? 0 : lba + hdf[unit].offset);
					// FIXME - READM could cross the fake RDB -> real disk boundary.
					// FIXME - but first we should make some attempt to generate fake RGB in multiple mode.

				    while (sector_count)
				    {
				        while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer

				        block_count = sector_count;
				        if (block_count > hdf[unit].sectors_per_block)
				            block_count = hdf[unit].sectors_per_block;

				        WriteStatus(IDE_STATUS_IRQ);

				        if (hdf[unit].file.size)
		//                    FileReadEx(&hdf[unit].file, NULL, block_count); // NULL enables direct transfer to the FPGA
				            FileReadEx(&hdf[unit].file, 0, block_count); // NULL enables direct transfer to the FPGA

						while (block_count--)
						{	
							if(sector_count!=1)
							{
								if (sector == hdf[unit].sectors)
								{
									sector = 1;
									head++;
									if (head == hdf[unit].heads)
									{
										head = 0;
										cylinder++;
									}
								}
								else
									sector++;
							}
							sector_count--;
						}	
						WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
		//					WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
					}
		//			WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
					break;
				case HDF_CARD:
				case HDF_CARDPART0:
				case HDF_CARDPART1:
				case HDF_CARDPART2:
				case HDF_CARDPART3:
					{
				        long lba=chs2lba(cylinder, head, sector, unit)+hdf[unit].offset;
					    while (sector_count)
					    {
					        while (!(GetFPGAStatus() & CMD_IDECMD)); // wait for empty sector buffer

						    block_count = sector_count;
						    if (block_count > hdf[unit].sectors_per_block)
						        block_count = hdf[unit].sectors_per_block;

					        WriteStatus(IDE_STATUS_IRQ);
							MMC_ReadMultiple(lba,0,block_count);
							lba+=block_count;
							
							while (block_count--)
							{	
								if(sector_count!=1)
								{
									if (sector == hdf[unit].sectors)
									{
										sector = 1;
										head++;
										if (head == hdf[unit].heads)
										{
											head = 0;
											cylinder++;
										}
									}
									else
										sector++;
										
								}
								sector_count--;
							}	
							WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
			//					WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
						}
			//			WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
					}
					break;
			}
        }
        else if (tfr[7] == ACMD_WRITE_SECTORS) // write sectors
        {
            WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type

            sector = tfr[3];
            cylinder = tfr[4] | (tfr[5] << 8);
            head = tfr[6] & 0x0F;
            sector_count = tfr[2];
            if (sector_count == 0)
                sector_count = 0x100;

		    long lba=chs2lba(cylinder, head, sector, unit);
//			if(hdf[unit].type>=HDF_CARDPART0)
				lba+=hdf[unit].offset;

		        if (hdf[unit].file.size)	// File size will be 0 in direct card modes
		            HardFileSeek(&hdf[unit], (lba>-1) ? lba : 0);

		        while (sector_count)
		        {
		            while (!(GetFPGAStatus() & CMD_IDEDAT)); // wait for full write buffer

	//				 decrease sector count
					if(sector_count!=1)
					{
						if (sector == hdf[unit].sectors)
						{
							sector = 1;
							head++;
							if (head == hdf[unit].heads)
							{
								head = 0;
								cylinder++;
							}
						}
						else
							sector++;
					}
	
					WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);

		            EnableFpga();
		            SPI(CMD_IDE_DATA_RD); // read data command
		            SPI(0x00);
		            SPI(0x00);
		            SPI(0x00);
		            SPI(0x00);
		            SPI(0x00);
		            for (i = 0; i < 512; i++)
		                sector_buffer[i] = SPI(0xFF);
		            DisableFpga();

		            sector_count--; // decrease sector count

		            if (sector_count)
		                WriteStatus(IDE_STATUS_IRQ);
		            else
		                WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);

					switch(hdf[unit].type)
					{
						case HDF_FILE | HDF_SYNTHRDB:
						case HDF_FILE:
						    if (hdf[unit].file.size && (lba>-1))	// Don't attempt to write to fake RDB
						    {
						        FileWrite(&hdf[unit].file, sector_buffer);
						        FileSeek(&hdf[unit].file, 1, SEEK_CUR);
						    }
							++lba;
							break;
						case HDF_CARD:
						case HDF_CARDPART0:
						case HDF_CARDPART1:
						case HDF_CARDPART2:
						case HDF_CARDPART3:
							MMC_Write(lba,sector_buffer);
							++lba;
							break;
					}
		        }
		} 
       else if (tfr[7] == ACMD_WRITE_MULTIPLE) // write sectors
        {
            WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type

            sector = tfr[3];
            cylinder = tfr[4] | (tfr[5] << 8);
            head = tfr[6] & 0x0F;
            sector_count = tfr[2];
            if (sector_count == 0)
                sector_count = 0x100;

		    long lba=chs2lba(cylinder, head, sector, unit);
//			if(hdf[unit].type>=HDF_CARDPART0)
				lba+=hdf[unit].offset;

		        if (hdf[unit].file.size)	// File size will be 0 in direct card modes
		            HardFileSeek(&hdf[unit], (lba>-1) ? lba : 0);

		        while (sector_count)
		        {
		            block_count = sector_count;
		            if (block_count > hdf[unit].sectors_per_block)
		                block_count = hdf[unit].sectors_per_block;

		            while (block_count)
		            {
		                while (!(GetFPGAStatus() & CMD_IDEDAT)); // wait for full write buffer

		//				 decrease sector count
						if(sector_count!=1)
						{
							if (sector == hdf[unit].sectors)
							{
								sector = 1;
								head++;
								if (head == hdf[unit].heads)
								{
									head = 0;
									cylinder++;
								}
							}
							else
								sector++;
						}
		
		//				WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);

				        EnableFpga();
				        SPI(CMD_IDE_DATA_RD); // read data command
				        SPI(0x00);
					    SPI(0x00);
				        SPI(0x00);
				        SPI(0x00);
				        SPI(0x00);
				        for (i = 0; i < 512; i++)
				            sector_buffer[i] = SPI(0xFF);
				        DisableFpga();
						switch(hdf[unit].type)
						{
							case HDF_FILE | HDF_SYNTHRDB:
							case HDF_FILE:
							    if (hdf[unit].file.size && (lba>-1))
							    {
							        FileWrite(&hdf[unit].file, sector_buffer);
							        FileSeek(&hdf[unit].file, 1, SEEK_CUR);
							    }
								++lba;
								break;
							case HDF_CARD:
							case HDF_CARDPART0:
							case HDF_CARDPART1:
							case HDF_CARDPART2:
							case HDF_CARDPART3:
								MMC_Write(lba,sector_buffer);
								++lba;
								break;
						}
		                block_count--;  // decrease block count
		                sector_count--; // decrease sector count
		            }
					WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);

		            if (sector_count)
		                WriteStatus(IDE_STATUS_IRQ);
		            else
		                WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
		        }
        }
        else
        {
            hdd_debugf("Unknown ATA command\r");

            hdd_debugf("IDE:");
            for (i = 1; i < 7; i++)
                hdd_debugf("%02X.", tfr[i]);
            hdd_debugf("%02X\r", tfr[7]);
            WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
            WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
        }
        DISKLED_OFF;
    }
}

void GetHardfileGeometry(hdfTYPE *pHDF)
{ // this function comes from WinUAE, should return the same CHS as WinUAE

    unsigned long total=0;
    unsigned long i, head, cyl, spt;
    unsigned long sptt[] = { 63, 127, 255, 0 };

	switch(pHDF->type)
	{
		case (HDF_FILE | HDF_SYNTHRDB):
		    if (pHDF->file.size == 0)
    		    return;
		    total = pHDF->file.size / 512;
			pHDF->heads = 1;
			pHDF->sectors = 32;
			pHDF->cylinders = total/32 + 1;	// Add a cylinder for the fake RDB.
			return;			
		case HDF_FILE:
		    if (pHDF->file.size == 0)
    		    return;
		    total = pHDF->file.size / 512;
			break;
		case HDF_CARD:
		    total = MMC_GetCapacity();	// GetCapacity returns number of blocks, not bytes.
			break;
		case HDF_CARDPART0:
		case HDF_CARDPART1:
		case HDF_CARDPART2:
		case HDF_CARDPART3:
		    total = partitions[pHDF->partition].sectors;
			break;
		default:
			break;
	}

    for (i = 0; sptt[i] != 0; i++)
    {
        spt = sptt[i];
        for (head = 4; head <= 16; head++)
        {
            cyl = total / (head * spt);
            if (total <= 1024 * 1024)
            {
                if (cyl <= 1023)
                    break;
            }
            else
            {
                if (cyl < 16383)
                    break;
                if (cyl < 32767 && head >= 5)
                    break;
                if (cyl <= 65535)	// Should there some head constraint here?
                    break;
            }
        }
        if (head <= 16)
            break;
    }
    pHDF->cylinders = (unsigned short)cyl;
    pHDF->heads = (unsigned short)head;
    pHDF->sectors = (unsigned short)spt;
}

void BuildHardfileIndex(hdfTYPE *pHDF)
{
    // builds index to speed up hard file seek

    fileTYPE *file = &pHDF->file;
    unsigned long *index = pHDF->index;
    unsigned long i;
    unsigned long j;

    pHDF->index_size = 16; // indexing size
    j = 1 << pHDF->index_size;
    i = pHDF->file.size >> 10; // divided by index table size (1024)
    while (j < i) // find greater or equal power of two
    {
        j <<= 1;
        pHDF->index_size++;
    }

    for (i = 0; i < file->size; i += j)
    {
        FileSeek(file, i >> 9, SEEK_SET); // FileSeek seeks in 512-byte sectors
        *index++ = file->cluster;
    }
}

unsigned char HardFileSeek(hdfTYPE *pHDF, unsigned long lba)
{
    if ((pHDF->file.sector ^ lba) & cluster_mask)
    { // different clusters
        if ((pHDF->file.sector > lba) || ((pHDF->file.sector ^ lba) & (cluster_mask << (fat32 ? 7 : 8)))) // 7: 128 FAT32 links per sector, 8: 256 FAT16 links per sector
        { // requested cluster lies before current pointer position or in different FAT sector
            pHDF->file.cluster = pHDF->index[lba >> (pHDF->index_size - 9)];// minus 9 because lba is in 512-byte sectors
            pHDF->file.sector = lba & (-1 << (pHDF->index_size - 9));
        }
    }
    return FileSeek(&pHDF->file, lba, SEEK_SET);
}

unsigned char OpenHardfile(unsigned char unit)
{
    unsigned long time;
    char filename[12];

	switch(config.hardfile[unit].enabled)
	{
		case HDF_FILE | HDF_SYNTHRDB:
		case HDF_FILE:
			hdf[unit].type=config.hardfile[unit].enabled;
			strncpy(filename, config.hardfile[unit].name, 8);
			strcpy(&filename[8], "HDF");

			if (filename[0])
			{
				if (FileOpen(&hdf[unit].file, filename))
				{
				    GetHardfileGeometry(&hdf[unit]);

				    hdd_debugf("HARDFILE %d:", unit);
				    hdd_debugf("file: \"%.8s.%.3s\"", hdf[unit].file.name, &hdf[unit].file.name[8]);
				    hdd_debugf("size: %lu (%lu MB)", hdf[unit].file.size, hdf[unit].file.size >> 20);
				    hdd_debugf("CHS: %u.%u.%u", hdf[unit].cylinders, hdf[unit].heads, hdf[unit].sectors);
				    hdd_debugf(" (%lu MB)", ((((unsigned long) hdf[unit].cylinders) * hdf[unit].heads * hdf[unit].sectors) >> 11));

				    time = GetTimer(0);
				    BuildHardfileIndex(&hdf[unit]);
				    time = GetTimer(0) - time;
				    hdd_debugf("Hardfile indexed in %lu ms", time >> 16);

					if(config.hardfile[unit].enabled & HDF_SYNTHRDB)
						hdf[unit].offset=-(hdf[unit].heads*hdf[unit].sectors);
					else
						hdf[unit].offset=0;		

				    config.hardfile[unit].present = 1;
				    return 1;
				}
			}
			break;
		case HDF_CARD:
			hdf[unit].type=HDF_CARD;
		    config.hardfile[unit].present = 1;
			hdf[unit].file.size=0;
			hdf[unit].offset=0;
		    GetHardfileGeometry(&hdf[unit]);
			return 1;
			break;
		case HDF_CARDPART0:
		case HDF_CARDPART1:
		case HDF_CARDPART2:
		case HDF_CARDPART3:
			hdf[unit].type=config.hardfile[unit].enabled;
			hdf[unit].partition=hdf[unit].type-HDF_CARDPART0;
		    config.hardfile[unit].present = 1;
			hdf[unit].file.size=0;
			hdf[unit].offset=partitions[hdf[unit].partition].startlba;
		    GetHardfileGeometry(&hdf[unit]);
			return 1;
			break;
	}
    config.hardfile[unit].present = 0;
    return 0;
}


fileTYPE rdbfile;	// We scan for RDB without mounting the file as a unit, so need a file struct specifically for this task.

unsigned char GetHDFFileType(char *filename)
{
	if(FileOpen(&rdbfile,filename))
	{
		int i;
		for(i=0;i<16;++i)
		{
			FileRead(&rdbfile,sector_buffer);
			FileSeek(&rdbfile,512,SEEK_CUR);
			if(sector_buffer[0]=='R' && sector_buffer[1]=='D' && sector_buffer[2]=='S' && sector_buffer[3]=='K')
				return(HDF_FILETYPE_RDB);
			if(sector_buffer[0]=='D' && sector_buffer[1]=='O' && sector_buffer[2]=='S')
				return(HDF_FILETYPE_DOS);
			if(sector_buffer[0]=='P' && sector_buffer[1]=='F' && sector_buffer[2]=='S')
				return(HDF_FILETYPE_DOS);
			if(sector_buffer[0]=='S' && sector_buffer[1]=='F' && sector_buffer[2]=='S')
				return(HDF_FILETYPE_DOS);
		}
		return(HDF_FILETYPE_UNKNOWN);
	}
	return(HDF_FILETYPE_NOTFOUND);
}


