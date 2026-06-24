## Getting Started

1. Navigate to the root directory of the GVSOC repository and add the following line to your `Makefile`:

   ```makefile
   include pulp/pulp/chips/softhier/softhier.mk
   ```

2. From the root directory, run the following command to install the required toolchains:

   ```bash
   source pulp/pulp/chips/softhier/softhier_init.sh
   ```

3. Ensure that the environment variables `CC`, `CXX`, and `CMAKE` are correctly set.

4. Compile the SoftHier hardware:

   ```bash
   make sh-hw
   ```

5. Compile the SoftHier software:

   ```bash
   make sh-sw
   ```

   The default application is located at:

   ```
   pulp/pulp/chips/softhier/sw/app_example
   ```

6. Run the simulation:

   ```bash
   make sh-run
   ```

7. Chips with specific topologies can be called as follows:

   ```bash
      make sh-hw TOPOLOGY=[topology]
      make sh-sw TOPOLOGY=[topology]
      make sh-run TOPOLOGY=[topology]
   ```

   Where [topology] can be 3d, torus, 3d_torus, ring, hierarchical_ring, hexamesh or fht (FoldedHexaTorus)

8. FlooGen works by adding a `floogen.yml` configuration file into a chip's folder. Keep in mind to comment out any manual instantiation.
9. Custom routing tables can be imported by adding a routing.yml file into a chip's folder.
   The connections should be defined as:

   ```yaml
   router_0_0:
      cluster_ni_1_1: router_1_0
      cluster_ni_0_2: router_0_1

   router_1_0:
      cluster_ni_1_1: cluster_ni_1_1
   ```

10. When using FlooGen, keep in mind to update the topology dimension in `softhier_arch.py`.