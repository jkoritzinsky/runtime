// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// ------------------------------------------------------------------------------
// Changes to this file must follow the https://aka.ms/api-review process.
// ------------------------------------------------------------------------------

namespace Microsoft.Extensions.Caching.Distributed
{
    public partial class MemoryDistributedCache : Microsoft.Extensions.Caching.Distributed.IDistributedCache
    {
        public MemoryDistributedCache(Microsoft.Extensions.Options.IOptions<Microsoft.Extensions.Caching.Memory.MemoryDistributedCacheOptions> optionsAccessor) { }
        public MemoryDistributedCache(Microsoft.Extensions.Options.IOptions<Microsoft.Extensions.Caching.Memory.MemoryDistributedCacheOptions> optionsAccessor, Microsoft.Extensions.Logging.ILoggerFactory loggerFactory) { }
        public byte[]? Get(string key) { throw null; }
        public System.Threading.Tasks.Task<byte[]?> GetAsync(string key, System.Threading.CancellationToken token = default(System.Threading.CancellationToken)) { throw null; }
        public void Refresh(string key) { }
        public System.Threading.Tasks.Task RefreshAsync(string key, System.Threading.CancellationToken token = default(System.Threading.CancellationToken)) { throw null; }
        public void Remove(string key) { }
        public System.Threading.Tasks.Task RemoveAsync(string key, System.Threading.CancellationToken token = default(System.Threading.CancellationToken)) { throw null; }
        public void Set(string key, byte[] value, Microsoft.Extensions.Caching.Distributed.DistributedCacheEntryOptions options) { }
        public System.Threading.Tasks.Task SetAsync(string key, byte[] value, Microsoft.Extensions.Caching.Distributed.DistributedCacheEntryOptions options, System.Threading.CancellationToken token = default(System.Threading.CancellationToken)) { throw null; }
    }
}
namespace Microsoft.Extensions.Caching.Memory
{
    public partial class MemoryCache : Microsoft.Extensions.Caching.Memory.IMemoryCache, System.IDisposable
    {
        public MemoryCache(Microsoft.Extensions.Options.IOptions<Microsoft.Extensions.Caching.Memory.MemoryCacheOptions> optionsAccessor) { }
        public MemoryCache(Microsoft.Extensions.Options.IOptions<Microsoft.Extensions.Caching.Memory.MemoryCacheOptions> optionsAccessor, Microsoft.Extensions.Logging.ILoggerFactory loggerFactory) { }
        public int Count { get { throw null; } }
        public System.Collections.Generic.IEnumerable<object> Keys { get { throw null; } }
        public void Clear() { }
        public void Compact(double percentage) { }
        public Microsoft.Extensions.Caching.Memory.ICacheEntry CreateEntry(object key) { throw null; }
        public void Dispose() { }
        protected virtual void Dispose(bool disposing) { }
        ~MemoryCache() { }
        public Microsoft.Extensions.Caching.Memory.MemoryCacheStatistics? GetCurrentStatistics() { throw null; }
        public void Remove(object key) { }
        public bool TryGetValue(object key, out object? result) { throw null; }

#if NET9_0_OR_GREATER
        [System.Runtime.CompilerServices.OverloadResolutionPriority(1)]
        public bool TryGetValue(System.ReadOnlySpan<char> key, out object? value) { throw null; }
        [System.Runtime.CompilerServices.OverloadResolutionPriority(1)]
        public bool TryGetValue<TItem>(System.ReadOnlySpan<char> key, out TItem? value) { throw null; }
#endif
    }
    public partial class MemoryCacheOptions : Microsoft.Extensions.Options.IOptions<Microsoft.Extensions.Caching.Memory.MemoryCacheOptions>
    {
        public MemoryCacheOptions() { }
        public Microsoft.Extensions.Internal.ISystemClock? Clock { get { throw null; } set { } }
        public double CompactionPercentage { get { throw null; } set { } }
        [System.ComponentModel.EditorBrowsableAttribute(System.ComponentModel.EditorBrowsableState.Never)]
        [System.Obsolete("This property is retained only for compatibility.  Remove use and instead call MemoryCache.Compact as needed.", error: true)]
        public bool CompactOnMemoryPressure { get; set; }
        public System.TimeSpan ExpirationScanFrequency { get { throw null; } set { } }
        Microsoft.Extensions.Caching.Memory.MemoryCacheOptions Microsoft.Extensions.Options.IOptions<Microsoft.Extensions.Caching.Memory.MemoryCacheOptions>.Value { get { throw null; } }
        public long? SizeLimit { get { throw null; } set { } }
        public bool TrackLinkedCacheEntries { get { throw null; } set { } }
        public bool TrackStatistics { get { throw null; } set { } }
    }
    public partial class MemoryDistributedCacheOptions : Microsoft.Extensions.Caching.Memory.MemoryCacheOptions
    {
        public MemoryDistributedCacheOptions() { }
    }
}
namespace Microsoft.Extensions.DependencyInjection
{
    public static partial class MemoryCacheServiceCollectionExtensions
    {
        public static Microsoft.Extensions.DependencyInjection.IServiceCollection AddDistributedMemoryCache(this Microsoft.Extensions.DependencyInjection.IServiceCollection services) { throw null; }
        public static Microsoft.Extensions.DependencyInjection.IServiceCollection AddDistributedMemoryCache(this Microsoft.Extensions.DependencyInjection.IServiceCollection services, System.Action<Microsoft.Extensions.Caching.Memory.MemoryDistributedCacheOptions> setupAction) { throw null; }
        public static Microsoft.Extensions.DependencyInjection.IServiceCollection AddMemoryCache(this Microsoft.Extensions.DependencyInjection.IServiceCollection services) { throw null; }
        public static Microsoft.Extensions.DependencyInjection.IServiceCollection AddMemoryCache(this Microsoft.Extensions.DependencyInjection.IServiceCollection services, System.Action<Microsoft.Extensions.Caching.Memory.MemoryCacheOptions> setupAction) { throw null; }
    }
}
