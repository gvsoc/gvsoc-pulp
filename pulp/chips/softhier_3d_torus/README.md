# SoftHier_3D

## Getting Started

1. Navigate to the root directory of the GVSOC repository and add the following line to your `Makefile`:

   ```makefile
   include pulp/pulp/chips/softhier_3d/softhier.mk
   ```

2. From the root directory, run the following command to install the required toolchains:

   ```bash
   source pulp/pulp/chips/softhier_3d/softhier_init.sh
   ```

3. Ensure that the environment variables `CC`, `CXX`, and `CMAKE` are correctly set.

4. Compile the SoftHier hardware:

   ```bash
   make sh-3d-hw
   ```

5. Compile the SoftHier_3D software:

   ```bash
   make sh-3d-test-sw
   ```

   The default application is located at:

   ```
   pulp/pulp/chips/softhier/sw/tests/00_init
   ```

6. Run the simulation (for the most recently compiled test):

   ```bash
   make sh-3d-test-run
   ```

7. Add new tests to the /tests folder.

8. Compile a specific test through:
   ```bash
   make sh-3d-test-sw TEST=<test_name>
   ```