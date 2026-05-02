/*
 * compat.h — fills gaps between NT 3.5's scsi.h and what
 * nvme2k expects (NT4 / W2K era).
 *
 * NT 3.5's <scsi.h> ships only the SCSI-2-era opcodes nvme2k's
 * SCSI-translation layer references several SCSI-3 opcodes (10/16
 * byte CDBs and ATA pass-through) that landed in later DDKs. Rather
 * than mutate the shared header in PRIVATE\NTOS\INC, we backfill
 * locally here and include this file from nvme2k.h - keeping the
 * port's gaps visible at one site.
 *
 * Values are SPC-3 / SBC-2 / SAT-3 standard opcodes; cross-checked
 * against the upstream nvme2k tree which targets these via newer
 * DDK headers that already define them.
 */
#ifndef _NVME2K_COMPAT_H_
#define _NVME2K_COMPAT_H_

#ifndef SCSIOP_MODE_SENSE10
#define SCSIOP_MODE_SENSE10         0x5A
#endif
#ifndef SCSIOP_READ16
#define SCSIOP_READ16               0x88
#endif
#ifndef SCSIOP_WRITE16
#define SCSIOP_WRITE16              0x8A
#endif
#ifndef SCSIOP_READ_CAPACITY16
#define SCSIOP_READ_CAPACITY16      0x9E
#endif
#ifndef SCSIOP_READ_DEFECT_DATA10
#define SCSIOP_READ_DEFECT_DATA10   0x37
#endif
#ifndef SCSIOP_ATA_PASSTHROUGH12
#define SCSIOP_ATA_PASSTHROUGH12    0xA1
#endif
#ifndef SCSIOP_ATA_PASSTHROUGH16
#define SCSIOP_ATA_PASSTHROUGH16    0x85
#endif

#endif /* _NVME2K_COMPAT_H_ */
