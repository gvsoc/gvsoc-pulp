# mc_pool: multi-cluster MemPool

mc_pool connects a mesh of MemPool clusters (`pulp/mempool`) and HBM channels on
the edges of the mesh, with FlooNoC (`pulp/floonoc_flex`). Each cluster is a 
MemPool cluster with its L2 instruction memory, stack memory, boot ROM, cluster 
CSR (synchronization) and a shared iDMA (`pulp/idma`). The folder follows the 
layout of `pulp/chips/softhier`.

## Configuration

The parameters of each configuration are in `mc_pool_arch_base.py` 
(`McPoolArch<Name>`, listed in `CONFIGS`). The tensorpool configuration reads 
`NUM_CORES`, `NUM_CORES_PER_TILE`, `NUM_SUB_GROUPS_PER_GROUP`, `BANK_FACTOR`, 
`BANK_SIZE`, `NUM_REDMULE_TILES`, `REDMULE_HEIGHT`, `REDMULE_WIDTH`, 
`REDMULE_REGS` and `TE_QUEUE_DEPTH` from the environment, like the 
single-cluster `tensorpool` model. Any field can also be overridden from the 
command line, e.g. `--config-opt system/num_cluster_x=4`.

The HBM channels are plain memories of `hbm_chan_size` bytes by default. 
Set `hbm_dramsys = True` to model them with DRAMSys: this needs SystemC and 
makes the simulation much slower.

## Build and run

```bash
source sourceme.sh
make build TARGETS="pulp.chips.mc_pool.configs.mc_pool_tensorpool_target"
gvrun --target pulp.chips.mc_pool.configs.mc_pool_tensorpool_target \
      --work-dir <work_dir> --param system/binary=<elf> run
```

The software is the MemPool (`https://github.com/pulp-platform/mempool`) 
multi-cluster runtime (`software/runtime/multi_cluster`).

```bash
cd <mempool_dir>
 make -C software/apps/multi_cluster config=tensorpool hello_world
```

The Makefile uses the default architecture headers 
`software/runtime/multi_cluster/arch/` (10 clusters, 256 PEs/cluster, 
16 8x32 RedMulEs/cluster). For any other configuration, generate them with 
`common/utils/config.py`. The runtime checks that the headers match `num_cores` 
and `num_redmule_tiles` used to build the software.

```bash
NUM_CORES=512 python3 pulp/pulp/chips/mc_pool/common/utils/config.py tensorpool <out_dir>
make <app> MC_ARCH_DIR=<out_dir> num_cores=512 ...
```

## Structure

| File | |
|---|---|
| `mc_pool_arch_base.py` | Configurations |
| `mc_pool_system_base.py` | System: clusters, mesh NoC, HBM channels and controllers, control registers |
| `mc_pool_target_base.py`, `configs/` | gvrun targets, one per configuration |
| `common/cluster_unit.py` | One cluster: MemPool cluster, L2, stack, boot ROM, cluster CSR, iDMA, NoC ports |
| `common/cluster_csr.*` | Cluster CSR, with the global barrier between clusters |
| `common/mc_pool_ctrl.*` | System control registers (EOC, timer, print), aligned to `softhier_ctrl` |
| `common/mc_pool_dma*.py`, `common/mc_pool_dma_ctrl.*` | Shared `SnitchDma` with the MemPool DMA frontend |
| `common/mc_pool_mesh_noc.py` | 2D mesh on FlooNoC Flex with XY routing |
| `common/hbm_ctrl.*` | HBM controller (address scrambling) |
| `common/util_dumpper.*` | Data dumper |
| `common/utils/config.py` | Software header generator |

## Address map

| Region | Base | Notes |
|---|---|---|
| Local L1 (TCDM) | `0x00000000` | |
| Stack | `0x10000000` | per cluster |
| Cluster CSR | `0x20000000` | barrier registers |
| Remote L1 | `0x30000000` | |
| MemPool control registers | `0x40000000` | EOC at `+0x0` |
| DMA frontend | `0x40010000` | |
| Synchronization | `0x50000000` | `sync_interleave` per cluster |
| L2 (instructions, data) | `0x80000000` | per cluster |
| System control registers | `0x90000000` | |
| Boot ROM | `0xa0000000` | MemPool boot ROM (`pulp/mempool/bootrom.bin`) |
| HBM | `0xc0000000` | `hbm_node_addr_space` per edge node |

The cores start at the ELF entry. After the EOC they return to the boot ROM.