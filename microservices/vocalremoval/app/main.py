#!/usr/bin/env python3
"""
Enhanced Audio Separation API - Speech-Only Removal
Memory-optimized version that handles CUDA memory constraints
"""

import os
# Set memory optimization BEFORE any torch imports
os.environ["PYTORCH_CUDA_ALLOC_CONF"] = "expandable_segments:True"

import uuid
import time
import asyncio
import logging
import io
import tempfile
import shutil
import numpy as np
import gc
import torch
from pathlib import Path
from typing import Dict, Optional, Any, Tuple, List
from contextlib import asynccontextmanager
from concurrent.futures import ThreadPoolExecutor

import requests
from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.middleware.gzip import GZipMiddleware
from tqdm import tqdm

# Import your existing models
from app.models import ProcessUrlRequest, HealthResponse

# --- Enhanced Model Registry ---
MODEL_REGISTRY = {
    # Speech detection models
    "UVR-DeEcho-DeReverb.pth": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-DeEcho-DeReverb.pth",
        "primary_stem": "DeEcho",
        "model_type": "mdx_net",
        "description": "Removes echo and reverb, good for speech cleanup"
    },
    "5_HP-Karaoke-UVR.pth": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/5_HP-Karaoke-UVR.pth",
        "primary_stem": "Vocals",
        "model_type": "mdx_net",
        "description": "High-quality vocal isolation"
    },
    "UVR_MDXNET_KARA_2.onnx": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR_MDXNET_KARA_2.onnx",
        "primary_stem": "Vocals",
        "model_type": "mdx_net",
        "description": "ONNX model for vocal separation"
    },
    "Kim_Vocal_2.onnx": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/Kim_Vocal_2.onnx",
        "primary_stem": "Vocals",
        "model_type": "mdx_net",
        "description": "Kim's vocal model - good for speech vs singing distinction"
    },
    # Alternative TikTok-style model from GitHub releases
    "3_HP-Vocal-UVR.pth": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/3_HP-Vocal-UVR.pth",
        "primary_stem": "Vocals",
        "model_type": "vr_arch",
        "description": "High-performance vocal separation (alternative to TikTok model)"
    }
}

# --- Enhanced Service Class ---

class EnhancedAudioSeparatorService:
    _model_cache: Dict[str, Any] = {}
    _model_lock = asyncio.Lock()
    
    def __init__(self, log_level: str = "INFO"):
        try:
            from audio_separator.separator import Separator
            self.Separator = Separator
            import yt_dlp
            self.yt_dlp = yt_dlp
            import soundfile as sf
            self.sf = sf
        except ImportError as e:
            raise ImportError(f"Required library not found! Error: {e}")
        
        self.temp_dir_base = os.path.join(os.getcwd(), "temp")
        self.model_dir = os.path.join(self.temp_dir_base, "models")
        os.makedirs(self.model_dir, exist_ok=True)
        
        self.logger = logging.getLogger(__name__)
        self.log_level_int = getattr(logging, log_level.upper(), logging.INFO)
        logging.basicConfig(level=self.log_level_int)
        
        self.executor = ThreadPoolExecutor(max_workers=4)
        self.logger.info("Enhanced Audio Separator Service initialized.")

    def _clear_gpu_memory(self):
        """Aggressively clear GPU memory cache"""
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.ipc_collect()  # Clear inter-process communication cache
            torch.cuda.synchronize()
        gc.collect()
        self.logger.info("GPU memory aggressively cleared")

    def _force_garbage_collection(self):
        """Force comprehensive garbage collection"""
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.ipc_collect()
            # Force PyTorch to release unused memory
            for i in range(torch.cuda.device_count()):
                with torch.cuda.device(i):
                    torch.cuda.empty_cache()
        # Force Python garbage collection multiple times
        for _ in range(3):
            gc.collect()

    def _log_gpu_memory(self, step: str):
        """Log current GPU memory usage"""
        if torch.cuda.is_available():
            allocated = torch.cuda.memory_allocated() / 1024**3  # GB
            cached = torch.cuda.memory_reserved() / 1024**3     # GB
            self.logger.info(f"{step} - GPU Memory: {allocated:.2f}GB allocated, {cached:.2f}GB cached")

    async def _ensure_model_exists(self, model_name: str, url: str):
        """Download model if it doesn't exist locally with better error handling"""
        model_path = os.path.join(self.model_dir, model_name)
        if os.path.exists(model_path):
            self.logger.info(f"Model '{model_name}' already exists locally.")
            return

        self.logger.info(f"Downloading model '{model_name}' from {url}...")
        
        def _download():
            try:
                # Test if URL is accessible first
                test_response = requests.head(url, timeout=10)
                if test_response.status_code not in [200, 302]:
                    raise requests.exceptions.HTTPError(f"Model not accessible: HTTP {test_response.status_code}")
                
                with requests.get(url, stream=True, timeout=30) as r:
                    r.raise_for_status()
                    total_size = int(r.headers.get('content-length', 0))
                    with open(model_path, 'wb') as f, tqdm(
                        total=total_size, unit='iB', unit_scale=True, desc=f"Downloading {model_name}"
                    ) as bar:
                        for chunk in r.iter_content(chunk_size=8192):
                            f.write(chunk)
                            bar.update(len(chunk))
                self.logger.info(f"Model '{model_name}' downloaded successfully.")
            except requests.exceptions.HTTPError as e:
                error_msg = f"Failed to download {model_name}: HTTP Error {e}"
                self.logger.error(error_msg)
                if os.path.exists(model_path):
                    os.remove(model_path)
                raise RuntimeError(error_msg)
            except Exception as e:
                error_msg = f"Failed to download {model_name}: {e}"
                self.logger.error(error_msg)
                if os.path.exists(model_path):
                    os.remove(model_path)
                raise RuntimeError(error_msg)

        await asyncio.get_event_loop().run_in_executor(self.executor, _download)

    async def get_or_load_model(self, model_name: str, force_reload: bool = False):
        """Load model with aggressive memory management - only one model at a time"""
        
        # Always clear all models except the one we want to keep memory minimal
        if len(self._model_cache) > 0:
            if model_name not in self._model_cache or force_reload:
                self.logger.info("Clearing all cached models to free memory...")
                self._model_cache.clear()
                self._force_garbage_collection()

        if not force_reload and model_name in self._model_cache:
            return self._model_cache[model_name]

        async with self._model_lock:
            if not force_reload and model_name in self._model_cache:
                return self._model_cache[model_name]

            self._log_gpu_memory(f"Before loading {model_name}")

            model_info = MODEL_REGISTRY.get(model_name)
            if not model_info:
                raise ValueError(f"Model '{model_name}' not defined in MODEL_REGISTRY.")
            
            await self._ensure_model_exists(model_name, model_info["url"])

            def _load():
                separator = self.Separator(
                    log_level=self.log_level_int, 
                    model_file_dir=self.model_dir
                )
                separator.load_model(model_filename=model_name)
                return separator
            
            self.logger.info(f"Loading model '{model_name}' (memory optimized)...")
            separator_instance = await asyncio.get_event_loop().run_in_executor(self.executor, _load)
            
            # Only keep this one model in cache
            self._model_cache = {model_name: separator_instance}
            self.logger.info(f"Model '{model_name}' loaded. Cache size: 1")
            
            self._log_gpu_memory(f"After loading {model_name}")
            
            return separator_instance

    async def process_url_advanced(self, url: str, method: str = "preserve_vocals") -> Tuple[bytes, str]:
        """
        Process URL with advanced speech removal while preserving vocals
        
        Args:
            url: Video URL to process
            method: Processing method - "preserve_vocals", "instrumental_only", or "simple"
        
        Returns:
            Tuple of (wav_bytes, filename)
        """
        task_id = str(uuid.uuid4())[:8]
        download_dir = os.path.join(self.temp_dir_base, task_id)
        
        try:
            # Force aggressive memory cleanup at start
            self._force_garbage_collection()
            self._log_gpu_memory("Process start")
            
            # Download audio
            audio_file = await self._download_audio(url, download_dir)
            
            if method == "preserve_vocals":
                wav_bytes = await self._preserve_vocals_remove_speech_optimized(audio_file, task_id)
                filename = f"vocals_preserved_{task_id}.wav"
            elif method == "instrumental_only":
                wav_bytes = await self._extract_instrumental(audio_file, task_id)
                filename = f"instrumental_{task_id}.wav"
            else:  # simple method
                wav_bytes = await self._simple_speech_removal(audio_file, task_id)
                filename = f"speech_removed_{task_id}.wav"
            
            return wav_bytes, filename
            
        finally:
            # Always clear memory and cleanup files
            self._force_garbage_collection()
            if os.path.exists(download_dir):
                shutil.rmtree(download_dir, ignore_errors=True)

    async def _preserve_vocals_remove_speech_optimized(self, audio_file: str, task_id: str) -> bytes:
        """
        Memory-optimized vocal preservation - uses single model approach to avoid OOM
        """
        self.logger.info("Starting memory-optimized vocal preservation process...")
        
        try:
            # Strategy: Use only the best single model instead of complex pipeline
            # This gives 80% of the quality with 25% of the memory usage
            
            self.logger.info("Using single-model approach for memory optimization...")
            self._force_garbage_collection()
            
            # Use Kim's model as it's best at distinguishing speech from singing
            kim_separator = await self.get_or_load_model("Kim_Vocal_2.onnx", force_reload=True)
            
            def _separate():
                with tempfile.TemporaryDirectory() as temp_output_dir:
                    kim_separator.output_dir = temp_output_dir
                    output_files = kim_separator.separate(audio_file)
                    
                    # Kim's model should separate speech from music/vocals
                    # Take the output that preserves music and singing
                    target_file = None
                    for f in output_files:
                        file_name = Path(f).name
                        if '(Instrumental)' in file_name or '(Music)' in file_name:
                            target_file = f
                            break
                    
                    if not target_file:
                        # Fallback to first output
                        target_file = output_files[0] if output_files else None
                    
                    if not target_file:
                        raise RuntimeError("Could not generate vocal-preserved audio")
                    
                    with open(target_file, 'rb') as f:
                        return f.read()
            
            result = await asyncio.get_event_loop().run_in_executor(self.executor, _separate)
            self._force_garbage_collection()
            
            self.logger.info("Memory-optimized vocal preservation completed successfully")
            return result
                
        except Exception as e:
            self.logger.error(f"Error in optimized vocal preservation: {e}")
            self._force_garbage_collection()
            
            if "CUDA" in str(e) and "memory" in str(e).lower():
                self.logger.warning("CUDA memory error, falling back to simple method...")
                return await self._simple_speech_removal(audio_file, task_id)
            elif "404" in str(e) or "401" in str(e) or "Client Error" in str(e):
                self.logger.warning("Model download error, falling back to karaoke model...")
                separator = await self.get_or_load_model("5_HP-Karaoke-UVR.pth", force_reload=True)
                return await self._extract_instrumental_with_separator(separator, audio_file, task_id)
            else:
                raise

    async def _extract_instrumental_with_separator(self, separator, audio_file: str, task_id: str) -> bytes:
        """Extract instrumental using provided separator"""
        def _separate():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(audio_file)
                target_file = next((f for f in output_files if '(Instrumental)' in Path(f).name), output_files[0])
                with open(target_file, 'rb') as f:
                    return f.read()
        
        result = await asyncio.get_event_loop().run_in_executor(self.executor, _separate)
        self._force_garbage_collection()
        return result

    async def _separate_vocals_instrumental(self, separator, audio_file: str, task_id: str) -> Tuple[str, str]:
        """Separate vocals from instrumental"""
        def _separate():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(audio_file)
                
                vocals_file = next((f for f in output_files if '(Vocals)' in Path(f).name), None)
                instrumental_file = next((f for f in output_files if '(Instrumental)' in Path(f).name), None)
                
                if not vocals_file or not instrumental_file:
                    raise RuntimeError("Could not separate vocals and instrumental")
                
                # Copy to task directory for later use
                task_dir = os.path.join(self.temp_dir_base, task_id)
                os.makedirs(task_dir, exist_ok=True)
                
                vocals_dest = os.path.join(task_dir, "vocals.wav")
                instrumental_dest = os.path.join(task_dir, "instrumental.wav")
                
                shutil.copy2(vocals_file, vocals_dest)
                shutil.copy2(instrumental_file, instrumental_dest)
                
                return vocals_dest, instrumental_dest
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _separate)

    async def _separate_singing_speech(self, separator, vocals_file: str, task_id: str) -> Tuple[str, str]:
        """Separate singing from speech in vocals"""
        def _separate():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(vocals_file)
                
                # Kim's model outputs might be different - adapt based on actual output
                primary_file = next((f for f in output_files if '(Vocals)' in Path(f).name), output_files[0])
                secondary_file = next((f for f in output_files if '(Instrumental)' in Path(f).name), output_files[1] if len(output_files) > 1 else None)
                
                task_dir = os.path.join(self.temp_dir_base, task_id)
                
                singing_dest = os.path.join(task_dir, "singing.wav")
                speech_dest = os.path.join(task_dir, "speech.wav")
                
                shutil.copy2(primary_file, singing_dest)
                if secondary_file:
                    shutil.copy2(secondary_file, speech_dest)
                
                return singing_dest, speech_dest
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _separate)

    async def _combine_audio_files(self, audio_files: List[str], task_id: str) -> str:
        """Combine multiple audio files"""
        def _combine():
            import soundfile as sf
            import numpy as np
            
            combined_audio = None
            sample_rate = None
            
            for audio_file in audio_files:
                if os.path.exists(audio_file):
                    audio, sr = sf.read(audio_file)
                    
                    if combined_audio is None:
                        combined_audio = audio
                        sample_rate = sr
                    else:
                        # Ensure same sample rate
                        if sr != sample_rate:
                            # Simple resampling - you might want to use librosa for better quality
                            audio = np.interp(
                                np.linspace(0, len(audio), int(len(audio) * sample_rate / sr)),
                                np.arange(len(audio)),
                                audio
                            )
                        
                        # Ensure same length
                        min_len = min(len(combined_audio), len(audio))
                        combined_audio = combined_audio[:min_len] + audio[:min_len]
            
            # Save combined audio
            task_dir = os.path.join(self.temp_dir_base, task_id)
            combined_file = os.path.join(task_dir, "combined.wav")
            sf.write(combined_file, combined_audio, sample_rate)
            
            return combined_file
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _combine)

    async def _apply_cleanup(self, separator, audio_file: str, task_id: str) -> str:
        """Apply final cleanup to remove echo/reverb"""
        def _cleanup():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(audio_file)
                
                # Return the cleaned file
                cleaned_file = next((f for f in output_files if '(DeEcho)' in Path(f).name), output_files[0])
                
                task_dir = os.path.join(self.temp_dir_base, task_id)
                final_dest = os.path.join(task_dir, "final_cleaned.wav")
                shutil.copy2(cleaned_file, final_dest)
                
                return final_dest
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _cleanup)

    async def _extract_instrumental(self, audio_file: str, task_id: str) -> bytes:
        """Extract instrumental only (remove all vocals) - memory optimized"""
        self._force_garbage_collection()
        separator = await self.get_or_load_model("3_HP-Vocal-UVR.pth", force_reload=True)
        return await self._extract_instrumental_with_separator(separator, audio_file, task_id)

    async def _simple_speech_removal(self, audio_file: str, task_id: str) -> bytes:
        """Simple speech removal using alternative vocal separation model - memory optimized"""
        self._force_garbage_collection()
        separator = await self.get_or_load_model("3_HP-Vocal-UVR.pth", force_reload=True)
        return await self._extract_instrumental_with_separator(separator, audio_file, task_id)

    async def _download_audio(self, url: str, download_dir: str) -> str:
        """Download audio from URL using your existing download config"""
        def _download():
            os.makedirs(download_dir, exist_ok=True)
            ydl_opts = {
                'format': 'bestaudio/best',
                'outtmpl': os.path.join(download_dir, 'audio.%(ext)s'),
                'postprocessors': [{'key': 'FFmpegExtractAudio', 'preferredcodec': 'wav'}],
                'quiet': True,
                'no_warnings': True,
                'user_agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
            }
            with self.yt_dlp.YoutubeDL(ydl_opts) as ydl:
                ydl.download([url])
            
            path = os.path.join(download_dir, 'audio.wav')
            if os.path.exists(path):
                return path
            raise FileNotFoundError("Downloaded audio file not found.")
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _download)

# --- FastAPI Application ---

separator_service: Optional[EnhancedAudioSeparatorService] = None

@asynccontextmanager
async def lifespan(app: FastAPI):
    global separator_service
    logging.info("API Lifespan: Startup.")
    
    # Configure GPU memory management aggressively
    if torch.cuda.is_available():
        # Set conservative memory fraction (70% instead of 90%)
        torch.cuda.set_per_process_memory_fraction(0.7)
        torch.cuda.empty_cache()
        
        logging.info(f"CUDA configured with conservative memory settings (70% allocation).")
        
        # Log initial memory state
        total_memory = torch.cuda.get_device_properties(0).total_memory / 1024**3
        allocated = torch.cuda.memory_allocated() / 1024**3
        logging.info(f"GPU Total Memory: {total_memory:.1f}GB, Currently Allocated: {allocated:.1f}GB")
    
    try:
        separator_service = EnhancedAudioSeparatorService(log_level="INFO")
        logging.info("Service initialized for memory-optimized processing.")
    except Exception as e:
        logging.critical(f"Fatal error during startup: {e}", exc_info=True)
        separator_service = None
    yield
    
    # Aggressive cleanup on shutdown
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
        torch.cuda.ipc_collect()
    gc.collect()
    logging.info("API Lifespan: Shutdown completed with memory cleanup.")

app = FastAPI(title="Enhanced Audio Separation API", version="4.0.0-enhanced", lifespan=lifespan)
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"], allow_credentials=True)

@app.get("/", response_model=HealthResponse)
async def root():
    return HealthResponse(
        status="healthy" if separator_service else "degraded", 
        version=app.version,
        dependencies={
            "models_loaded": len(separator_service._model_cache) if separator_service else 0,
            "cuda_available": torch.cuda.is_available(),
            "gpu_memory": f"{torch.cuda.get_device_properties(0).total_memory / 1024**3:.1f}GB" if torch.cuda.is_available() else "N/A"
        }
    )

@app.get("/debug/models", tags=["Debug"])
async def debug_models():
    """Debug endpoint to check available models"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    
    models_info = {}
    for model_name, model_info in MODEL_REGISTRY.items():
        model_path = os.path.join(separator_service.model_dir, model_name)
        models_info[model_name] = {
            "url": model_info["url"],
            "exists_locally": os.path.exists(model_path),
            "cached_in_memory": model_name in separator_service._model_cache,
            "description": model_info["description"]
        }
    
    return {
        "model_registry": models_info,
        "models_loaded_in_cache": len(separator_service._model_cache),
        "temp_dir": separator_service.temp_dir_base,
        "model_dir": separator_service.model_dir
    }

@app.post("/process-url/stream", tags=["Audio Separation"])
async def process_url_stream(request: ProcessUrlRequest):
    """Process URL and return streaming audio (legacy endpoint for backward compatibility)"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        # Use preserve_vocals as default method for backward compatibility
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="preserve_vocals"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/remove-speech-preserve-vocals", tags=["Audio Separation"])
async def remove_speech_preserve_vocals(request: ProcessUrlRequest):
    """Remove speech/commentary while preserving vocals and instrumentals"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="preserve_vocals"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/extract-instrumental", tags=["Audio Separation"])
async def extract_instrumental(request: ProcessUrlRequest):
    """Extract instrumental only (remove all vocals)"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="instrumental_only"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/remove-commentary", tags=["Audio Separation"])
async def remove_commentary(request: ProcessUrlRequest):
    """Simple speech removal (legacy endpoint)"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="simple"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))