"""
Combined optimizations: Model Caching + FastAPI Performance
This integrates both optimization strategies for maximum performance
"""

import os
import uuid
import time
import asyncio
import multiprocessing as mp
from pathlib import Path
from typing import Dict, Optional, Callable
from contextlib import asynccontextmanager
from concurrent.futures import ThreadPoolExecutor
import io
import tempfile
import shutil

from fastapi import FastAPI, HTTPException, File, UploadFile
from fastapi.responses import StreamingResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.middleware.gzip import GZipMiddleware
import uvicorn

from .models import ProcessUrlRequest, HealthResponse


class CombinedOptimizedSeparator:
    """
    Combined optimizations: Model caching + Performance tuning
    """
    
    # Class-level model cache (Optimization 1)
    _model_cache: Dict[str, any] = {}
    _model_lock = asyncio.Lock()
    
    def __init__(self, 
                 output_dir: str = None,
                 temp_dir: str = None,
                 output_format: str = "wav",
                 sample_rate: int = 44100,
                 model_file_dir: str = None,
                 log_level: str = "INFO",
                 progress_callback: Optional[Callable] = None,
                 max_workers: int = None):
        
        # Import dependencies
        try:
            from audio_separator.separator import Separator
            self.Separator = Separator
        except ImportError:
            raise ImportError("audio-separator library not found!")
        
        try:
            import yt_dlp
            self.yt_dlp = yt_dlp
        except Exception as e:
            raise ImportError(f"yt-dlp library not working! Error: {e}")
        
        # Setup directories
        self.output_dir = output_dir or os.path.join(os.getcwd(), "output")
        self.temp_dir = temp_dir or os.path.join(os.getcwd(), "temp")
        os.makedirs(self.output_dir, exist_ok=True)
        os.makedirs(self.temp_dir, exist_ok=True)
        
        # Auto-detect optimal settings based on hardware
        cpu_count = mp.cpu_count()
        self.is_older_cpu = cpu_count <= 4
        
        # Optimization 2: Optimized thread pools
        if max_workers is None:
            if self.is_older_cpu:
                max_workers = min(2, cpu_count)
            else:
                max_workers = min(4, cpu_count)
        
        self.io_executor = ThreadPoolExecutor(max_workers=max_workers)
        self.cpu_executor = ThreadPoolExecutor(max_workers=1)  # AI inference
        
        # Setup logging
        import logging
        self.logger = logging.getLogger(__name__)
        self.progress_callback = progress_callback
        
        # Model configuration
        model_dir = model_file_dir or os.path.join(self.temp_dir, "models")
        os.makedirs(model_dir, exist_ok=True)
        
        self.model_config = {
            'output_dir': self.output_dir,
            'output_format': output_format.upper(),
            'sample_rate': sample_rate,
            'model_file_dir': model_dir,
            'log_level': getattr(logging, log_level.upper()),
            'output_single_stem': "Instrumental"
        }
        
        self.separator = None
        self.current_model = None
        
        # Performance tracking
        self.stats = {
            'total_requests': 0,
            'avg_processing_time': 0,
            'cache_hits': 0,
            'cache_misses': 0
        }
        
        print(f"🚀 Initialized with {max_workers} workers, CPU optimization: {self.is_older_cpu}")
    
    async def get_or_load_model(self, model_name: str = "model_bs_roformer_ep_317_sdr_12.9755.ckpt"):
        """
        Optimization 1: Model caching with thread-safe loading
        """
        async with self._model_lock:
            if model_name in self._model_cache:
                self.separator = self._model_cache[model_name]
                self.current_model = model_name
                self.stats['cache_hits'] += 1
                self._update_progress(f"✅ Using cached model: {model_name}")
                return
            
            # Load model in optimized thread pool
            def _load_model():
                separator = self.Separator(**self.model_config)
                separator.load_model(model_filename=model_name)
                return separator
            
            self._update_progress(f"🤖 Loading model: {model_name}")
            separator = await asyncio.get_event_loop().run_in_executor(
                self.cpu_executor, _load_model
            )
            
            # Cache the loaded model
            self._model_cache[model_name] = separator
            self.separator = separator
            self.current_model = model_name
            self.stats['cache_misses'] += 1
            self._update_progress(f"✅ Model loaded and cached: {model_name}")
    
    async def download_audio_optimized(self, url: str, task_id: str = None) -> str:
        """
        Optimization 2: Fast downloads with optimized settings
        """
        def _download():
            temp_subdir = os.path.join(self.temp_dir, task_id or str(uuid.uuid4())[:8])
            os.makedirs(temp_subdir, exist_ok=True)
            
            # Optimized yt-dlp settings
            ydl_opts = {
                'format': 'best[height<=720]/best[ext=mp4]/best[ext=webm]/best/worst',
                'outtmpl': os.path.join(temp_subdir, 'audio.%(ext)s'),
                'postprocessors': [{
                    'key': 'FFmpegExtractAudio',
                    'preferredcodec': 'wav',
                    'preferredquality': '0' if not self.is_older_cpu else '192',
                }],
                'quiet': True,
                'no_warnings': True,
                'extract_flat': False,
                'writethumbnail': False,
                'writeinfojson': False,
                'concurrent_fragment_downloads': 2,
                'retries': 3,
                'socket_timeout': 30,
                'user_agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
                'no_check_certificate': True,
            }
            
            with self.yt_dlp.YoutubeDL(ydl_opts) as ydl:
                ydl.download([url])
                
                # Find downloaded file efficiently
                temp_path = Path(temp_subdir)
                for ext in ['.wav', '.m4a', '.webm', '.mp3', '.ogg']:
                    files = list(temp_path.glob(f"*{ext}"))
                    if files:
                        return str(files[0])
                
                # Fallback
                all_files = [f for f in temp_path.iterdir() if f.is_file()]
                if all_files:
                    return str(all_files[0])
                else:
                    raise FileNotFoundError("No downloaded file found")
        
        return await asyncio.get_event_loop().run_in_executor(self.io_executor, _download)
    
    async def separate_audio_to_memory_optimized(self, audio_path: str) -> bytes:
        """
        Optimization 1 + 2: Use cached model + memory streaming
        """
        def _separate():
            import soundfile as sf
            
            if not self.separator:
                raise RuntimeError("No model loaded - call get_or_load_model() first")
            
            # Create minimal temp directory
            temp_output_dir = tempfile.mkdtemp(prefix="audio_stream_")
            
            try:
                # Store and temporarily change output directory
                original_output_dir = self.separator.output_dir
                self.separator.output_dir = temp_output_dir
                
                # Also update model instance if needed
                restore_model_output = None
                if hasattr(self.separator, 'model_instance') and hasattr(self.separator.model_instance, 'output_dir'):
                    restore_model_output = self.separator.model_instance.output_dir
                    self.separator.model_instance.output_dir = temp_output_dir
                
                # Perform separation using cached model
                output_files = self.separator.separate(audio_path)
                
                # Find instrumental file efficiently
                instrumental_file = None
                for file in output_files:
                    if 'instrumental' in Path(file).name.lower() and os.path.exists(file):
                        instrumental_file = file
                        break
                
                if not instrumental_file:
                    # Search in temp directory
                    for item in os.listdir(temp_output_dir):
                        if 'instrumental' in item.lower():
                            instrumental_file = os.path.join(temp_output_dir, item)
                            break
                
                if not instrumental_file or not os.path.exists(instrumental_file):
                    raise RuntimeError(f"No instrumental file found. Output: {output_files}")
                
                # Read and convert to WAV bytes in memory
                audio_data, sample_rate = sf.read(instrumental_file)
                buffer = io.BytesIO()
                sf.write(buffer, audio_data, sample_rate, format='WAV')
                wav_bytes = buffer.getvalue()
                buffer.close()
                
                return wav_bytes
                
            finally:
                # Restore original settings
                self.separator.output_dir = original_output_dir
                if restore_model_output is not None:
                    self.separator.model_instance.output_dir = restore_model_output
                
                # Quick cleanup
                try:
                    shutil.rmtree(temp_output_dir)
                except:
                    pass
        
        return await asyncio.get_event_loop().run_in_executor(self.cpu_executor, _separate)
    
    async def process_url_to_memory_combined(self, 
                                           input_source: str,
                                           task_id: str = None,
                                           **kwargs) -> bytes:
        """
        Combined optimizations: Cached model + optimized pipeline
        """
        start_time = time.time()
        
        if not task_id:
            task_id = str(uuid.uuid4())[:8]
        
        try:
            # Optimization 1: Ensure model is cached and ready
            await self.get_or_load_model()
            
            # Determine if this is a URL or file
            if self.is_url(input_source):
                # Optimization 2: Fast download
                self._update_progress("🌐 Downloading audio", 20)
                audio_file = await self.download_audio_optimized(input_source, task_id)
                cleanup_file = audio_file
            else:
                audio_file = input_source
                cleanup_file = None
            
            # Combined: Use cached model + memory streaming
            self._update_progress("🎵 Separating audio (using cached model)", 60)
            wav_bytes = await self.separate_audio_to_memory_optimized(audio_file)
            
            # Optimization 2: Fast cleanup
            if cleanup_file and os.path.exists(cleanup_file):
                try:
                    os.remove(cleanup_file)
                    parent_dir = os.path.dirname(cleanup_file)
                    if task_id in parent_dir:
                        shutil.rmtree(parent_dir, ignore_errors=True)
                except:
                    pass
            
            # Update performance stats
            processing_time = time.time() - start_time
            self.stats['total_requests'] += 1
            self.stats['avg_processing_time'] = (
                (self.stats['avg_processing_time'] * (self.stats['total_requests'] - 1) + processing_time) 
                / self.stats['total_requests']
            )
            
            self._update_progress("🎉 Processing complete!", 100)
            return wav_bytes
            
        except Exception as e:
            # Cleanup on error
            if 'cleanup_file' in locals() and cleanup_file and os.path.exists(cleanup_file):
                try:
                    os.remove(cleanup_file)
                    parent_dir = os.path.dirname(cleanup_file)
                    if task_id in parent_dir:
                        shutil.rmtree(parent_dir, ignore_errors=True)
                except:
                    pass
            raise e
    
    def is_url(self, input_string: str) -> bool:
        """Check if input string is a URL"""
        return input_string.startswith(('http://', 'https://', 'www.'))
    
    def _update_progress(self, message: str, progress: float = None):
        """Update progress via callback"""
        if self.progress_callback:
            self.progress_callback(message, progress)
        self.logger.info(message)
    
    def get_performance_stats(self) -> dict:
        """Get detailed performance statistics"""
        cache_hit_ratio = (
            self.stats['cache_hits'] / (self.stats['cache_hits'] + self.stats['cache_misses'])
            if (self.stats['cache_hits'] + self.stats['cache_misses']) > 0 else 0
        )
        
        return {
            **self.stats,
            'cache_hit_ratio': f"{cache_hit_ratio:.2%}",
            'model_cache_size': len(self._model_cache),
            'current_model': self.current_model,
            'cpu_optimization': self.is_older_cpu,
            'thread_pools': {
                'io_workers': self.io_executor._max_workers,
                'cpu_workers': self.cpu_executor._max_workers
            }
        }
    
    def __del__(self):
        """Cleanup executors"""
        if hasattr(self, 'io_executor'):
            self.io_executor.shutdown(wait=False)
        if hasattr(self, 'cpu_executor'):
            self.cpu_executor.shutdown(wait=False)


# Global separator instance with combined optimizations
combined_separator: Optional[CombinedOptimizedSeparator] = None

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan with model pre-loading"""
    global combined_separator
    
    print("🚀 Starting Combined Optimized Audio Separator API...")
    
    # Initialize directories
    output_dir = os.path.join(os.getcwd(), "output")
    temp_dir = os.path.join(os.getcwd(), "temp")
    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(temp_dir, exist_ok=True)
    
    try:
        # Initialize combined optimized separator
        combined_separator = CombinedOptimizedSeparator(
            output_dir=output_dir,
            temp_dir=temp_dir,
            log_level="INFO"
        )
        
        # Pre-load the model during startup (Optimization 1)
        print("🤖 Pre-loading AI model for instant responses...")
        await combined_separator.get_or_load_model()
        print("✅ Combined optimizations ready!")
        
    except Exception as e:
        print(f"⚠️ Separator initialization failed: {e}")
        combined_separator = None
    
    yield
    
    # Shutdown
    print("🛑 Shutting down...")

# Create FastAPI app with combined optimizations
app = FastAPI(
    title="Combined Optimized Audio Separator API",
    description="Maximum performance: Model caching + FastAPI optimizations",
    version="3.0.0",
    lifespan=lifespan
)

# Optimization 2: Performance middleware
app.add_middleware(GZipMiddleware, minimum_size=1000)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

def check_separator_available():
    """Check if separator is available"""
    if combined_separator is None:
        raise HTTPException(
            status_code=503, 
            detail="Audio separator not available. Service starting up or missing dependencies."
        )

@app.get("/", response_model=HealthResponse)
async def root():
    """Root endpoint with combined performance stats"""
    stats = {}
    if combined_separator:
        stats = combined_separator.get_performance_stats()
    
    return HealthResponse(
        status="healthy" if combined_separator is not None else "starting",
        version="3.0.0",
        dependencies={
            "separator_available": combined_separator is not None,
            "optimization_level": "combined",
            **stats
        }
    )

@app.post("/process-url/stream")
async def process_url_stream_combined(request: ProcessUrlRequest):
    """
    MAXIMUM PERFORMANCE: Combined model caching + FastAPI optimizations
    
    Expected performance:
    - First request: ~8-12 seconds (one-time model loading)
    - Subsequent requests: ~2-5 seconds (cached model + optimizations)
    """
    check_separator_available()
    
    start_time = time.time()
    task_id = str(uuid.uuid4())[:8]
    
    try:
        # Use combined optimizations
        wav_bytes = await combined_separator.process_url_to_memory_combined(
            input_source=str(request.url),
            task_id=task_id,
            sample_rate=request.sample_rate,
            channels=request.channels,
            bitrate=request.bitrate
        )
        
        processing_time = time.time() - start_time
        
        return StreamingResponse(
            io.BytesIO(wav_bytes),
            media_type="audio/wav",
            headers={
                "Content-Disposition": "attachment; filename=instrumental.wav",
                "Content-Length": str(len(wav_bytes)),
                "X-Processing-Time": f"{processing_time:.2f}s",
                "X-Optimization-Level": "combined",
                "Access-Control-Expose-Headers": "Content-Length,X-Processing-Time,X-Optimization-Level"
            }
        )
        
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Processing failed: {str(e)}")

@app.get("/performance-stats")
async def get_detailed_performance_stats():
    """Get comprehensive performance statistics"""
    if not combined_separator:
        return {"error": "Separator not available"}
    
    return {
        "optimization_level": "combined",
        "separator_stats": combined_separator.get_performance_stats(),
        "system_info": {
            "cpu_count": mp.cpu_count(),
            "older_cpu_mode": combined_separator.is_older_cpu
        }
    }

if __name__ == "__main__":
    # Optimization 2: Production server settings
    uvicorn.run(
        "app.main:app",
        host="0.0.0.0",
        port=8000,
        workers=1,  # Critical: keep workers=1 for model caching
        reload=False,
        log_level="info",
        access_log=False,  # Reduce I/O overhead
        loop="uvloop" if not os.name == 'nt' else "asyncio"  # Use uvloop on non-Windows
    )