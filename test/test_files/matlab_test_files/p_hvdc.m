function p_hvdc(t)

global PQ

powers = [0.9000 0.3000; 1.0000 0.3500; 1.2500 0.5000];
if t < 1
  PQ.con(:,[4 5]) = powers;
else
  PQ.con(:,[4 5]) = (1+(t-1)/10)*powers;
end
