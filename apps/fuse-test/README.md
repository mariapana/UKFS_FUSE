Build and Run Instructions:

1. Navigate to the app directory:
   cd /home/mia/Documents/Licenta/apps/fuse-test

2. Configure the application:
   make menuconfig

   Required Selections:
   - Platform Configuration → KVM guest
   - Library Configuration → ukfusefs: FUSE wrapper for UKFS
   - Library Configuration → ukdebug: Debugging and tracing

3. Save and exit the configuration

4. Build the application:
   make

5. Run the application in QEMU:
   qemu-system-x86_64 -kernel build/fuse-test_qemu-x86_64 -nographic -append verbose