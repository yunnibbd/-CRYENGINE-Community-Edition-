# CRYENGINE Community Edition
CRYENGINE Community Edition is a new features and improvements developed and maintained by the community. It builds on the foundation of CRYENGINE while introducing new features, improvements, and customization options contributed by developers in their free time.
## Installation
- Download the source code for CRYENGINE 5.7
- After you get cryengine 5.7.1 source code, 
- Download  CRYENGINE_V5.7.0_SDKs.zip  manually,  Extract it to EngineRootFolder/Code/SDKs. then  replace the patch files.
- place empty zip file near cry_cmake.exe and rename it to CRYENGINE_V5.7.0_SDKs.zip.
- run cmake (or cry_cmake.exe) and generate solutions. 

Why this step required for cry_cmake.exe( it's a known bug) :
Sometimes cry_cmake.exe can't download SDKs properly and tries to extract broken sdks archive file. By the way Archive is broken and cry_cmake.exe don't know About it and continues the operation.

# Fix:
- download the CRYENGINE_V5.7.0_SDKs.zip manually, extract to engine root folder/code/sdks
- place empty zip file near cry_cmake.exe and rename it to CRYENGINE_V5.7.0_SDKs.zip. Cry_cmake.exe assumes sdks was downloaded and skips download step and saves your time and also it  reduces broken download risk. Then run cry_cmake.exe and generate solutions.

## License&nbsp;
All files provided here are licensed under the MIT License, meaning you are free to use them in your own projects at no cost.The engine itself remains under the CRYENGINE license provided by Crytek. You must comply with the terms of that license when using the engine.Before downloading or using these files, please review the official CRYENGINE license on the CRYENGINE website&nbsp;to ensure you fully understand the requirements.Thank you for respecting both licenses and supporting the community effort.
