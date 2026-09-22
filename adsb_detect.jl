using DataStructures, FFTW, Plots
include("polyphase.jl")

plotlyjs()
default(ticks=:native, size=(1000,800))

function messge_length(format)
    if format in (0,4,5,11)
        return 56
    elseif format in (16,17,18,19,20,21,24)
        return 112
    else
        return 0
    end
end

crc = function(M)
    G = UInt128(0x1fff409) << (112-25)
    mask0 = UInt128(0x1000000)<<(112-25)
    mask1 = UInt128(0x1FFFFFF) << (112-25)
    for i in 112:-1:25
        if (0 != mask0 & M)
            M = ~mask1 & M + xor(mask1 & M, G)
        end
        mask0 >>= 1
        mask1 >>= 1
        G >>= 1
    end
    return M
end

fn = "gqrx_20260825_191707_1089700400_3200000_fc.sigmf-data"
N = Int(filesize(fn) / sizeof(ComplexF32) ÷ 65536)
x = Matrix{ComplexF32}(undef, 65536, N)
read!(fn, x)
nf = sqrt(mean(abs2.(x)))
x ./= nf
X = similar(x)
for j in 1:size(x,2)
    X[:,j] = fftshift(fft(x[:,j]))
end
db(x) = 10log10(real(x*conj(x)))
heatmap(db.(maximum(reshape(abs.(X),64,1024,:), dims=1))[1,:,:])

decode = function(b)
    n = UInt128(0)
    for _b in b
        n <<= 1
        if _b == 1
            n |= 0x1
        end
    end
    return n
    #sum(_b*(UInt128(0x1)<<(i-1)) for (i,_b) in enumerate(reverse(b)))
end

mutable struct ADSBDemod{C<:Number,R<:Real}
    src::CircularBuffer{C}
    y::CircularBuffer{R}
    rx::R
    t::R
    τ::R
    p::PolyphaseFilterBank{R}
    function ADSBDemod(::Type{C}, p::PolyphaseFilterBank{R}, sps::Real) where {C<:Number, R<:Real}
        Mh = size(p.h,1)÷2
        src = CircularBuffer{C}(zeros(C, size(p.h,1)))
        y = CircularBuffer{R}(zeros(R, 12*150))
        rx = 1/sps
        return new{C,R}(src, y, R(1/sps), 0, -Mh*rx, p)
    end
end

function exec!(d::ADSBDemod{C,R}, x::AbstractVector{C}) where {C<:Number, R<:Real}
    results = []
    Np = size(d.p.h,2)
    ix = 1
    ry = 1/12
    inv_ry = 12
    pat = [1,-1,1,-1,-1,-1, -1,1,-1,1,-1,-1, -1,-1,-1,-1]
    while ix <= length(x)
        ip = Int(round((d.τ - d.t) * inv_ry))
        while ip < 0
            if ix == length(x)
                return results
            end
            @inbounds push!(d.src, x[ix])
            ix += 1
            d.τ += d.rx
            ip += Np
        end
        push!(d.y, abs(dot(view(d.p.h, :,1+ip,1), d.src)))
        d.t += ry

        c::R = 0
        for i in 0:15
            @inbounds c += d.y[1+i*6] * pat[1+i]
        end
        if c < 3
            continue
        end
        ic = 1
        for i in 2:10
            c2::R = 0
            for j in 0:15
                @inbounds c2 += d.y[i+j*6] * pat[1+j]
            end
            if c2 > c
                ic = i
                c = c2
            end
        end
        mag::R = 0
        m::UInt128 = 0
        for j in ic+8*12:12:ic+119*12
            m <<= 1
            @inbounds df = d.y[j] - d.y[j+6]
            mag += abs(df)
            if df > 0
                m += 1
            end
        end
        if mag > 2*56
            push!(results, (Int(round((d.τ-length(d.y)*ry)/d.rx)), m))
        end
        d.t += (ic + 119*12)*ry
    end
    return results
end

d = ADSBDemod(ComplexF32, PolyphaseFilterBank(Float32, 33, 64, 0), 3.2)
results=[]
@time for i in 1:size(x,2)
    r = adsb_detect(p, x[:,i])
    append!(results,r)
end

let n = 62, short = true
    i = results[n][1]
    yi = i-12*100:i+150*4
    display((i, yi))
    y = resample(p, x[yi], 12/3.2)
    pat = [1,-1,1,-1,-1,-1, -1,1,-1,1,-1,-1, -1,-1,-1,-1]
    c = [sum(abs(y[i+j*6]) * pat[1+j] for j in 0:15) for i in 1:length(y)-15*6]
    k = min(argmax(c), length(y)-6-12*63)
    plot(abs.(y))
    plot!(c)
    L = short ? 63 : 119
    x1 = k:12:k+12*L
    x0 = k+6:12:k+6+12*L
    m::UInt128 = 0
    for (x1,x0) in zip(x1[9:end],x0[9:end])
        m <<= 1
        if abs(y[x1]) > abs(y[x0])
            m += 1
        end
    end
    df = Int((m>>(56-5))&0x1F)
    ca = Int((m>>(56-8))&0x07)
    icao = string((m>>(56-32))&0xFFFFFF, base=16)
    pi = string(m&0xFFFFFF, base=16)
    display((m, df, ca, icao, pi, crc(m)))
    plot!(x1, abs.(y[x1]), marker = :x, line = nothing)
    plot!(x0, abs.(y[x0]), marker = :x, line = nothing)
end
results = [adsb_detect(x[:,j]) for j in 1:50]
[[Int((r.m>>51)&0x1F) for r in _r] for _r in results]

for n = 1:length(results)
    for (i,r) in enumerate(results[n])
        display((n, i, Int((r.m>>107)&0x1F), r.m, crc(r.m)))
    end
end
let n = 38
    for r in results[n]
        display((r.bits1[1][1], Int((r.m>>107)&0x1F), r.m, crc(r.m), bitstring(r.m)[end-111:end]))
    end
    p = PolyphaseFilterBank(Float32, 101, 32, 3)
    y = resample(p, abs.(x[:,n]), 12/3.2)[:,1]
    pat = [1,-1,1,-1,-1,-1, -1,1,-1,1,-1,-1, -1,-1,-1,-1]
    c = [sum(y[i+j*6] * pat[1+j] for j in 0:15) for i in 1:length(y)-12*15]
    plot(y)
    plot!(c)
    bits0 = vcat((r.bits0 for r in results[n])...)
    bits1 = vcat((r.bits1 for r in results[n])...)
    plot!([b[1] for b in bits1], [b[2] for b in bits1], marker = :x, line = nothing)
    plot!([b[1] for b in bits0], [b[2] for b in bits0], marker = :x, line = nothing)
end

close(radio)
radio = RtlSdr()
set_rate(radio, 3.2e6)
set_freq(radio, 1090e6)
set_agc_mode(radio, 0)
set_tuner_gain_mode(radio, 0)
results = []
det = ADSBDemod(ComplexF64, PolyphaseFilterBank(Float64, 33, 64, 0), 3.2)
for i in 1:(2*3.2e6/8192)
    yr = read_samples(radio, 8192)
    yr /= sqrt(mean(abs2.(yr)))
    append!(results, exec!(det, yr))
end

known_icaos = Dict()
for (i,(j,m)) in enumerate(results)
    fmt = Int((m>>107)%0x1F)
    short = fmt in (0,4,5,11)
    long = fmt in (16,17,18,19,20,21,24)
    m2 = m
    if short
        m2 = m >> 56
    end
    if 0 == crc(m2)
        known_icaos[(m >> 80) & 0xFFFFFF] = []
    end
end
for (i,(j,m)) in enumerate(results[:])0
    fmt = Int((m>>107)%0x1F)
    ca = Int((m>>104)%0x07)
    icao = (m >> 80) & 0xFFFFFF
    short = fmt in (0,4,5,11)
    long = fmt in (16,17,18,19,20,21,24)
    m2 = m
    if short
        m2 = m >> 56
    end
    crc_val = crc(m2)
    crc_valid = (
        fmt == 11 ? (crc_val < 63) :
        fmt in (0,4,5,16,20,21) ? (crc_val in keys(known_icaos)) :
        fmt in (17,18) ? (crc_valid = 0 == crc_val) : false)
    display((i,j,(short||long),crc_valid,fmt,icao,m2,crc_val,bitstring(m2)[end-(short ? 55 : 111):end]))
    if 17 == fmt && crc_valid
        tp = Int((m2 >>(112-37))&0x1F)
        push!(known_icaos[icao], (tp, (m2 >> (112>>24)&((0x1<<56)-1))))
    end
end
known_icaos
pms.decode([Int((m>>107)%0x1F) in (0,4,5,11) ? m >>56 : m for (_,m) in results])
for (j,m) in results
    fmt = Int((m>>107)%0x1F)
    if fmt in (0,4,5,11)
        m >>= 56
    end
    display(pms.decode(m))
end