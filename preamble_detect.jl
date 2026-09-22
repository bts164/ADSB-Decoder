using DataStructures, LinearAlgebra, Plots
include("polyphase.jl")
plotlyjs()

struct ADSBPreambleDetector{T<:Real}
    sps_in::Float64
    sps_out::Int
    h::Array{T,3}
    function ADSBPreambleDetector(::Type{T}, sps_in, sps_out, M, Np, Nd = 3) where T<:Real
        return new{T}(sps_in, sps_out, reverse(PolyphaseFilterBank(T, M, Np, Nd).h, dims=1))
    end
end
d = ADSBPreambleDetector(Float32, 3.2, 8, 33, 32)

mutable struct Interpolator{T<:Real,U<:Number}
    τ::Float64
    rate::Float64
    buffer::CircularBuffer{U}
    h::Array{T,3}
    Interpolator{T,U}(sps, h) where {T<:Real, U<:Number} = new{T,U}(
        (-size(h,1)÷2 + 0.5)/sps, 1/sps, CircularBuffer{U}(zeros(U, size(h,1))), h)
end
h = reverse(PolyphaseFilterBank(Float32, 101, 32, 3).h, dims=1)
i = Interpolator{Float32, Float32}(3, h)

function (i::Interpolator{T,U})(t::Float64; d = 0) where {T<:Number, U<:Real}
    Np = size(i.h,2)
    p = clamp((t - i.τ) / i.rate * Np + 1 + Np÷2, 1, Np)
    return dot(i.buffer, view(i.h, :, p, d + 1))
end

function domain(i::Interpolator{T,U}) where {T<:Number, U<:Real}
    return (i.τ - i.rate / 2, i.τ + i.rate / 2)
end

function Base.push!(i::Interpolator{T,U}, x) where {T<:Number, U<:Real}
    push!(i.buffer, U(x))
    i.τ += i.rate
end

i = Interpolator{Float32,Float32}(3, h)
for j in 1:size(h,1)÷2
    push!(x, sin(2π*j/size(h,1)))
    push!(i, x[end])
end
T = i.rate * size(h,1)
t = 0
j = 1
y = ([], [], [])
for t in 0.1:0.1:2π
    while domain(i)[2] < t
        push!(i, sin(2π*j/size(h,1)))
        j += 1
    end
    push!(y[1], i(t;d=0))
    push!(y[2], i(t;d=1))
    push!(y[3], i(t;d=2))
end

function exec(d::ADSBPreambleDetector{T}, x::AbstractVector{U}) where {T<:Real, U<:Number}
    M = size(d.h,1)
    Np = size(d.h,2)
    src = Interpolator{T,U}(d.sps_in, d.sps_out, d.h)
    y = CircularBuffer{U}(zeros(U, 122*d.sps_out))
    #dy = CircularBuffer{U}(zeros(U, 122*d.sps_out))
    pattern = [1,0, 1,0, 0,0, 0,1, 0,1, 0,0, 0,0, 0,0]

    results = []
    t = 0
    ix = 1
    tobit(_x) = abs(_x) > 2.5 ? 1 : 0
    while ix <= length(x)
        t += rate_out
        while domain(i)[2] < t
            push!(src, x[ix])
            ix += 1
            if ix > length(x)
                break
            end
        end
        dy = src(t, d = 1)
        d2y = src(t, d = 2)
        t += 1e-3* real(y[end] * conj(dy[end]) + dy[end] * conj(y[end]))
        push!(y, src(t; d=0))

        if all(tobit(y[end-(15-i)*d.sps_out÷2]) == pattern[1+i] for i in 0:15)
            for _i in 1:112*d.sps_out
                t += rate_out
                p = Int(round((t-τ) / rate_in * Np))  + Np÷2
                while p > Np
                    push!(buffer, x[ix])
                    ix += 1
                    τ += rate_in
                    p -= Np
                    if ix > length(x)
                        return results
                    end
                end
                push!(y, dot(buffer, view(d.h, :, p, 1)))
                push!(dy, dot(buffer, view(d.h, :, p, 2)) / rate_out)
                dp = real(y[end] * conj(dy[end]) + dy[end] * conj(y[end])) < 0 ? -1 : 1
                if abs(y[end]) < 2.5
                    dp = -dp
                end
            end
            fi = Vector(dy)
            fo = Vector(y)
            b = [tobit(y[i]) for i in 1+8*d.sps_out:d.sps_out:1+119*d.sps_out]
            #c = [abs2(y[i+d.sps_out÷2]) > cutoff ? 0 : 1 for i in 1+8*d.sps_out:d.sps_out:1+119*d.sps_out]
            #@timeit to "decode" mb = decode(b)
            mb = decode(b)
            push!(results, (fo, fi, b, mb, crc(mb)))
            if length(results) >= 5
                return results
            end
        end
    end
    return results
end

xf = filt(digitalfilter(Lowpass(2.0/3.2), FIRWindow(hamming(51))),x[:])
plot(fftshift(fft(digitalfilter(Lowpass(2.0/3.2), FIRWindow(hamming(51))))).|>db)
y = DSP.resample(x[:,16], 3.2/6)
d = ADSBPreambleDetector(Float32, 3.2, 16, 101, 64)
x = zeros(ComplexF32, 100)
x[2:17] = ComplexF32(1/2) .+ [1,-1, 1,-1, -1,-1, -1,1, -1,1, -1,-1, -1,-1, -1,-1]/2
x2 = copy(x[:,1])
o = exec(d, x[:])

let
    Mh = size(d.h,1)÷2
    Np = size(d.h,2)
    # y = hcat((conv(o[1][2], reverse(view(d.h, :,p,1)))[Mh+1:end-Mh] for p in 1:Np)...)
    # dy = hcat((conv(o[1][2], reverse(view(d.h, :,p,2)))[Mh+1:end-Mh] for p in 1:Np)...)
    # display(size(y))
    # y = transpose(y)[:]
    # dy = transpose(dy)[:]
    # _y = real.(y .* conj.(y))
    # _dy = real.(y .* conj.(dy) + dy .* conj.(y))
    y = o[2][1][end-16*120:1:end]
    dy = o[2][2][end-16*120:1:end]
    _y = real.(y .* conj.(y))
    _dy = real.(y .* conj.(dy) + dy .* conj.(y))
    plot(LinRange(1, length(_y)/8, length(_y)), _y, marker=:x, size = (1000,800), ticks=:native)
    plot!(LinRange(1, length(_dy)/8, length(_dy)), _dy/10, marker=:x)
    plot!(LinRange(1, length(_y)/8, length(_y[1:8:end])), _y[1:8:end], marker=:x)
    #plot([_y], marker=:x, size = (1000,800), ticks=:native)
end

let y = o[1][1][1+0*8:1+(8+65)*8]
    N = length(y[1:8:end])
    plot(LinRange(1,N,length(y)), y.|>abs2, marker = :x, size = (1000,800), ticks=:native)
    plot!(y[1:8:end].|>abs2, marker = :x, ticks=:native)
    plot!(5b, marker = :x)
end
b = [abs2(x) > 5 ? 1 : 0 for x in o[1][1][3+9*8:8:3+(8+56)*8]]
m = decode(b)
crc(m)
c = [abs(x) > 0.5 ? 0 : 1 for x in o[68:8:68+8*111]]
o[68:8:68+8*111]
plot([abs.(b) abs.(b)])

m = decode(b)
crc(m)
