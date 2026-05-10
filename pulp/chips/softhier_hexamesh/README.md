# SoftHier

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
